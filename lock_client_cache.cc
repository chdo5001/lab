// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>


static void *
releasethread(void *x)
{
  lock_client_cache *cc = (lock_client_cache *) x;
  cc->releaser();
  return 0;
}

int lock_client_cache::last_port = 0;

lock_client_cache::lock_client_cache(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{
	seqid = 0;
	wait_for_revoke_cond = false;
  srand(time(NULL)^last_port);
  rlock_port = ((rand()%32000) | (0x1 << 10));
  const char *hname;
  // assert(gethostname(hname, 100) == 0);
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlock_port;
  id = host.str();
  last_port = rlock_port;
  rpcs *rlsrpc = new rpcs(rlock_port);
  int r;
  /* register RPC handlers with rlsrpc */
	rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke);
	rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry);
	int ret = cl->call(lock_protocol::subscribe, cl->id(), id, r);
  assert (ret == lock_protocol::OK);
  pthread_t th;
  r = pthread_create(&th, NULL, &releasethread, (void *) this);
  assert (r == 0);
	pthread_mutex_init(&map_lock, NULL);
	pthread_cond_init(&lock_free, NULL);
	pthread_cond_init(&revoke_cond, NULL);
}

lock_client_cache::~lock_client_cache() {
	pthread_mutex_destroy(&map_lock);
	pthread_cond_destroy(&lock_free);
	pthread_cond_destroy(&revoke_cond);
}

void
lock_client_cache::releaser()
{

  // This method should be a continuous loop, waiting to be notified of
  // freed locks that have been revoked by the server, so that it can
  // send a release RPC.
	rlock_protocol::lockid_t lid;
	int ret;
	int r;
	int retry;
	while (true) {
		pthread_mutex_lock(&map_lock);
		while (l_released.empty()) {
			//printf("l_released is empty\n");
			pthread_cond_wait(&lock_free, &map_lock);
			//printf("Releaser on client woke up\n");
		}
		//printf("Trying to release locks on the server\n");
		lid = l_released.front();
		l_released.pop_front();
		if (m_lock_status.count(lid) == 0) {
			//printf("Don't have lock anymore\n");
			continue;
		}
		if (m_lock_waitlist[lid].empty()) {
			retry = 0;
		} else {
			retry = 1;
		}
		m_lock_status[lid] = NONE;
		m_lock_seqid.erase(lid);
		pthread_mutex_unlock(&map_lock);
		// if retry == 1, the server notifies this client when lock lid is free again
		//printf("Trying to release lock %016llx for client %d on the server (Retry? 1==%d)\n", lid, cl->id(), retry);
		ret = cl->call(lock_protocol::release, cl->id(), lid, retry, r);
		assert (ret == 0);
		//pthread_mutex_lock(&map_lock);
		
		//pthread_mutex_unlock(&map_lock);
	}

}


lock_protocol::status
lock_client_cache::acquire(rlock_protocol::lockid_t lid)
{
	lock_protocol::status ret = lock_protocol::OK;
	int r;
	pthread_mutex_lock(&map_lock);
	if (m_lock_status.count(lid) == 0) {
		// Never saw this lock before. Create an status map entry 
		m_lock_status[lid] = NONE;
	}
  acqu:
	// The lock is not hold by the client. Try to acquire it.
	if (m_lock_status[lid] == NONE) {
		//printf("Lock %016llx not assigned. Acquire it for client %d thread %016lx\n", lid, cl->id(), pthread_self());
		m_lock_status[lid] = ACQUIRING;
		//assert(m_lock_waitlist[lid].empty()); 
		pthread_cond_t* cond = new pthread_cond_t();
		pthread_cond_init(cond, NULL);
		// The acquiring thread should be the first in the waitlist. I think this is not necessary, but just fair.
		m_lock_waitlist[lid].push_front(cond);
		seqid++;
		//while (m_lock_status[lid] == ACQUIRING && m_lock_thread[lid] != pthread_self()) {
		while(true) {
			pthread_mutex_unlock(&map_lock);
			ret = cl->call(lock_protocol::acquire, cl->id(), lid, seqid, r);
			//printf("ret = %d\n", ret);
			if (ret == lock_protocol::OK) {
				// Successful
				//printf("Sucessfully acquired lock %016llx for client %d %016lx\n", lid, cl->id(), pthread_self());
				pthread_mutex_lock(&map_lock);
				assert(m_lock_waitlist[lid].front() == cond);
				
				m_lock_waitlist[lid].pop_front();
				pthread_cond_destroy(cond);
				m_lock_status[lid] = LOCKED;
				m_lock_thread[lid] = pthread_self();
				m_lock_seqid[lid] = seqid;
				// Is there already a revoke request for this acquire?
				if (m_waiting_revoke.count(lid) != 0) {
					if (m_waiting_revoke[lid] == seqid) {
						l_revoke.push_back(lid);
						m_waiting_revoke.erase(lid);
					}
				}
				/*
				if (wait_for_revoke_cond) {
					printf("Have to wait for revoke to get ready\n");
					pthread_cond_wait(&revoke_cond, &map_lock);
				}
				pthread_cond_signal(&revoke_cond);*/
				pthread_mutex_unlock(&map_lock);

				return lock_protocol::OK;
			}
			if (ret == lock_protocol::RETRY) {
				// Unsuccessful. Retry later
				//printf("Not acquired lock %016llx for client %d thread %016lx. Retry later\n", lid, cl->id(), pthread_self());
				pthread_mutex_lock(&map_lock);
				//while(m_lock_status[lid] != ) {
				//printf("client %d thread %016lx waits at cond %p\n", cl->id(), pthread_self(), &cond);
				m_cond_waiting[cond] = true;
				pthread_cond_signal(cond);
				pthread_cond_wait(cond, &map_lock);
				//printf("Woke up by retryer. at retry. client %d thread %016lx\n", cl->id(), pthread_self());
				//seqid++;
				//printf("Woke up again\n");
				//assert(m_lock_waitlist[lid].front() == cond);
			}
		}
	}
		/*
			m_lock_waitlist[lid].pop_front();
			pthread_cond_destroy(cond);
			m_lock_status[lid] = LOCKED;
			m_lock_thread[lid] = pthread_self();
			if (m_waiting_revoke.count(lid) != 0) {
				if (m_waiting_revoke[lid] == seqid) {
					l_revoke.push_back(lid);
					m_waiting_revoke.erase(lid);
				}
			}
			pthread_mutex_unlock(&map_lock);
		}*/
	//	return ret;
	
	
	// If the calling thread already holds this lock just return
	if (m_lock_thread.count(lid) != 0) {
		if (pthread_equal(pthread_self(), m_lock_thread[lid]) != 0) {
			//printf("Lock %016llx already hold by thread %016lx on client %d\n", lid, pthread_self(), cl->id());
			pthread_mutex_unlock(&map_lock);
			return ret;
		}
	}
	
	
	// If the lock is FREE, assign it to the calling thread
	if (m_lock_status[lid] == FREE) {
		//printf("Lock %016llx is FREE. Assign it to thread %016lx on client %d\n", lid, pthread_self(), cl->id());
		m_lock_status[lid] = LOCKED;
		m_lock_thread[lid] = pthread_self();
		pthread_mutex_unlock(&map_lock);
		return ret;
	}
	
	// If the client holds the lock, but it is locked/releasing/acquiring, wait for it
	if (m_lock_status[lid] > 1) {
		//printf("Thread %016lx : Client %d has lock %016llx but its l/r/a. Have to wait\n", pthread_self(), cl->id(), lid);
		pthread_cond_t* cond = new pthread_cond_t();
		pthread_cond_init(cond, NULL);
		m_lock_waitlist[lid].push_back(cond);
		//while(m_lock_status[lid] != FREE) {
		m_cond_waiting[cond] = true;
		pthread_cond_wait(cond, &map_lock);
		//printf("Woke up again by retry. at l/r/a. client %d thread %016lx\n", cl->id(), pthread_self());
			//}
		assert(m_lock_waitlist[lid].front() == cond);
		m_lock_waitlist[lid].pop_front();
		pthread_cond_destroy(cond);
		//m_lock_status[lid] = LOCKED;
		//m_lock_thread[lid] = pthread_self();
		goto acqu;
		/*
		if (m_waiting_revoke.count(lid) != 0) {
			if (m_waiting_revoke[lid] == seqid) {
				l_revoke.push_back(lid);
				m_waiting_revoke.erase(lid);
			}
		}*/
		
	}
	
	pthread_mutex_unlock(&map_lock);
	return ret;
}

lock_protocol::status
lock_client_cache::release(rlock_protocol::lockid_t lid)
{
	//printf("Free lock %016llx by thread %016lx on client %d\n", lid, pthread_self(), cl->id());
	lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&map_lock);
	// Does this thread really holds the lock?
	if (m_lock_thread.count(lid) != 0) {
		if (m_lock_thread[lid] != pthread_self()) {
			//printf("Thread %016lx does not hold lock, but %016lx\n", pthread_self(), m_lock_thread[lid]);
			pthread_mutex_unlock(&map_lock);
			return ret;
		}
	}
	m_lock_thread.erase(lid);
	bool contains = false;
	//printf("Comparing l_revoke\n");
	for (std::list<rlock_protocol::lockid_t>::iterator it = l_revoke.begin(); it != l_revoke.end(); it++) {
		//printf("l_revoke: compare %016llx = %016llx\n", *it, lid);
		if (*it == lid) {
			//printf("Found\n");
			contains = true;
			break;
		}
	}
	if (contains) {
		//printf("Have to release this lock on server. client %d thread %016lx\n", cl->id(), pthread_self());
		m_lock_status[lid] = RELEASING;
		l_released.push_back(lid);
		l_revoke.remove(lid);
		pthread_cond_signal(&lock_free);
		pthread_mutex_unlock(&map_lock);
		return ret;
	} else {
		//printf("Set lock to FREE.  client %d thread %016lx\n", cl->id(), pthread_self());
		m_lock_status[lid] = FREE;
		if (!m_lock_waitlist[lid].empty()) {
			pthread_cond_signal(m_lock_waitlist[lid].front());
		}
	}
	pthread_mutex_unlock(&map_lock);
	return ret;
}

lock_protocol::status 
lock_client_cache::revoke(int clid, rlock_protocol::lockid_t lid, rlock_protocol::seqid_t seqid, int&) 
{
	//printf("Revoke called for lock %016llx on client %d\n", lid, clid);
	lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&map_lock);
	//wait_for_revoke_cond = true;
	while (m_lock_status[lid] == ACQUIRING) {
		//printf("thread %016lx on client %d waits acquire finish in revoke\n", pthread_self(), clid);
		//pthread_cond_signal(&revoke_cond);
		//pthread_cond_wait(&revoke_cond, &map_lock);
		pthread_mutex_unlock(&map_lock);
		sleep(1);
		pthread_mutex_lock(&map_lock);
		//printf("thread %016lx on client %d signaled acquire in revoke\n", pthread_self(), clid);
	}
	//printf("thread %016lx on client %d signaled acquire in revoke\n", pthread_self(), clid);
	//wait_for_revoke_cond = false;
	if (m_lock_seqid.count(lid) == 0) {
		//printf("m_lock_seqid[lid] is empty on client %d. Seems the lock already has been released\n", clid);
		return ret;
	}
	//assert(m_lock_seqid.count(lid) != 0);
	//printf("Compare seqids: %016llx = %016llx\n", m_lock_seqid[lid], seqid);
	// Only revoke if seqids match. No match -> Depricated or early revoke msg
	if (m_lock_seqid[lid] == seqid) {
		if (m_lock_status[lid] == FREE) {
			//printf("The lock is already free. Don't have to wait for release.");
			m_lock_status[lid] = RELEASING;
			l_released.push_back(lid);
			pthread_cond_signal(&lock_free);
		} else {
			l_revoke.push_back(lid);
		}
	} //else {
		//printf("No match\n");
	//}
	// Early revoke. This should not be possible due to the above condition
	if (m_lock_seqid[lid] < seqid) {
		m_waiting_revoke[lid] = seqid;
	}
	pthread_mutex_unlock(&map_lock);
	return ret;
}

lock_protocol::status
lock_client_cache::retry(int clid, rlock_protocol::lockid_t lid, int&)
{
	//printf("Retry called for lock %016llx on client %d\n", lid, clid);
	lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&map_lock);
	if (!m_lock_waitlist[lid].empty()) {
		pthread_cond_t* cond = m_lock_waitlist[lid].front();
		//printf("client %d thread %016lx signal cond at %p\n", clid, pthread_self(), cond);
		// This is necessary because it is possible that the acquiring thread does not wait at the condition yet.
		while (m_cond_waiting.count(cond) == 0) {
			//printf("have to wait for condition\n");
			pthread_cond_wait(cond, &map_lock);
		}
		m_cond_waiting.erase(cond);
		//printf("Signal retry.\n");
		//pthread_cond_signal(m_lock_waitlist[lid].front());
		pthread_cond_signal(cond);
		//m_lock_waitlist[lid].pop_front();
		//printf("Signaled\n");
	} //else {
		//printf("m_lock_waitlist[%016llx] is empty on client %d\n", lid, clid);
	//}
	pthread_mutex_unlock(&map_lock);
	return ret;
}