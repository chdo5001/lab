// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>

#define COND_TIMEOUT 100000

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
	seqid = 1;
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
	rlock_protocol::lockid_t lid;
	int ret;
	int r;
	int retry;
	struct timeval now;
	struct timespec timeout;
	
	while (true) {
		pthread_mutex_lock(&map_lock);
		while (l_released.empty()) {
			//printf("l_released is empty\n");
			gettimeofday(&now, NULL);
			timeout.tv_sec = now.tv_sec;
			timeout.tv_nsec = (now.tv_usec * 1000) + COND_TIMEOUT;
			ret = pthread_cond_timedwait(&lock_free, &map_lock, &timeout);
			//printf("Releaser on client woke up\n");
		}
		//printf("Trying to release locks on the server\n");
		lid = l_released.front();
		l_released.pop_front();
		assert (m_lock_status[lid] == RELEASING);
		if (m_lock_waitlist[lid]->empty()) {
			retry = 0;
		} else {
			retry = 1;
		}
		m_lock_status[lid] = NONE;
		pthread_mutex_unlock(&map_lock);
		// if retry == 1, the server notifies this client when lock lid is free again
		printf("Trying to release lock %016llx for client %d on the server (Retry? 1==%d)\n", lid, cl->id(), retry);
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
		// Never saw this lock before. Create map entries for it
		m_lock_status[lid] = NONE;
		m_lock_seqid[lid] = -1;
		m_waiting_revoke[lid] = -1;
		m_lock_thread[lid] = -1;
		m_lock_waitlist[lid] = new std::list<pthread_cond_t*>();
		m_lock_retry[lid] = false;
	}
  acqu:
	// The lock is not hold by the client. Try to acquire it.
	if (m_lock_status[lid] == NONE) {
		printf("Lock %016llx not assigned. Acquire it for client %d thread %016lx\n", lid, cl->id(), pthread_self());
		//m_lock_waitlist[lid].push_front(cond);
		seqid++;
		pthread_cond_t* cond = new pthread_cond_t();
		pthread_cond_init(cond, NULL);
		m_cond_waiting[cond] = false;
		while(true) {
			m_lock_status[lid] = ACQUIRING;
			pthread_mutex_unlock(&map_lock);
			ret = cl->call(lock_protocol::acquire, cl->id(), lid, seqid, r);
			if (ret == lock_protocol::OK) {
				// Successful
				printf("Sucessfully acquired lock %016llx for client %d %016lx\n", lid, cl->id(), pthread_self());
				pthread_mutex_lock(&map_lock);
				//assert(m_lock_waitlist[lid].front() == cond);
				// Don't have to wait. Condition is not needed anymore
				//m_lock_waitlist[lid].pop_front();
				m_cond_waiting.erase(cond);
				pthread_cond_destroy(cond);
				m_lock_status[lid] = LOCKED;
				m_lock_thread[lid] = pthread_self();
				m_lock_seqid[lid] = seqid;
				// Is there already a revoke request for this acquire?
				if (m_waiting_revoke[lid] == seqid) {
					l_revoke.push_back(lid);
					m_waiting_revoke[lid] = -1;
				}
				pthread_cond_signal(&revoke_cond);
				pthread_mutex_unlock(&map_lock);
				return lock_protocol::OK;
			}
			if (ret == lock_protocol::RETRY) {
				// Unsuccessful. Retry later
				pthread_mutex_lock(&map_lock);
				// Check if a retry rpc has arrived meanwhile. If its the only thread on this client 
				// that wants to acquire this lock, the m_lock_waitlist[lid] is empty at this moment and the retryer
				// did nothing when the retry rpc arrived.
				if (m_lock_retry[lid] == true) {
					//m_lock_status[lid] = NONE
					m_lock_retry[lid] = false;
					//goto acqu;
					continue;
				}
				m_lock_waitlist[lid]->push_front(cond);
				printf("Not acquired lock %016llx for client %d thread %016lx. Retry later\n", lid, cl->id(), pthread_self());
				//printf("client %d thread %016lx waits at cond %p\n", cl->id(), pthread_self(), &cond);
				m_cond_waiting[cond] = true;
				//pthread_cond_signal(cond);
				m_lock_status[lid] = RETRYING;
				pthread_cond_wait(cond, &map_lock);
				//printf("Woke up by retryer. at retry. client %d thread %016lx\n", cl->id(), pthread_self());
			}
		}
	}
		
	// If the calling thread already holds this lock just return
	if (m_lock_thread[lid] == pthread_self()) {
		if (pthread_equal(pthread_self(), m_lock_thread[lid]) != 0) {
			printf("Lock %016llx already hold by thread %016lx on client %d\n", lid, pthread_self(), cl->id());
			pthread_mutex_unlock(&map_lock);
			return ret;
		}
	}
	
	// If the lock is FREE, assign it to the calling thread
	if (m_lock_status[lid] == FREE) {
		printf("Lock %016llx is FREE. Assign it to thread %016lx on client %d\n", lid, pthread_self(), cl->id());
		m_lock_status[lid] = LOCKED;
		m_lock_thread[lid] = pthread_self();
		pthread_mutex_unlock(&map_lock);
		return ret;
	}
	
	// If the client holds the lock, but it is locked/releasing/acquiring, wait for it
	if (m_lock_status[lid] > 1) {
		printf("Thread %016lx : Client %d has lock %016llx but its l/r/a. Have to wait\n", pthread_self(), cl->id(), lid);
		pthread_cond_t* cond = new pthread_cond_t();
		pthread_cond_init(cond, NULL);
		m_lock_waitlist[lid]->push_back(cond);
		m_cond_waiting[cond] = true;
		pthread_cond_wait(cond, &map_lock);
		//printf("Woke up again by retry. at l/r/a. client %d thread %016lx\n", cl->id(), pthread_self());
			//}
		m_cond_waiting.erase(cond);
		pthread_cond_destroy(cond);
		goto acqu;
	}
	// This should never be reached
	assert(false);
	pthread_mutex_unlock(&map_lock);
	return ret;
}

lock_protocol::status
lock_client_cache::release(rlock_protocol::lockid_t lid)
{
	printf("Free lock %016llx by thread %016lx on client %d\n", lid, pthread_self(), cl->id());
	lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&map_lock);
	// Does this thread really holds the lock?
	if (m_lock_thread[lid] != pthread_self()) {
		printf("Thread %016lx does not hold lock, but %016lx\n", pthread_self(), m_lock_thread[lid]);
		pthread_mutex_unlock(&map_lock);
		return ret;
	}
	m_lock_thread[lid] = -1;
	m_lock_retry[lid] = false;
	/*
	bool contains = false;
	//printf("Comparing l_revoke\n");
	for (std::list<rlock_protocol::lockid_t>::iterator it = l_revoke.begin(); it != l_revoke.end(); it++) {
		//printf("l_revoke: compare %016llx = %016llx\n", *it, lid);
		if (*it == lid) {
			//printf("Found\n");
			contains = true;
			break;
		}
	}*/
	if (listContains<lock_protocol::lockid_t>(l_revoke, lid)) {
		//printf("Have to release this lock on server. client %d thread %016lx\n", cl->id(), pthread_self());
		m_lock_status[lid] = RELEASING;
		m_waiting_revoke[lid] = -1;
		m_lock_seqid[lid] = -1;
		l_released.push_back(lid);
		l_revoke.remove(lid);
		pthread_cond_signal(&lock_free);
		pthread_mutex_unlock(&map_lock);
		return ret;
	} else {
		//printf("Set lock to FREE.  client %d thread %016lx\n", cl->id(), pthread_self());
		m_lock_status[lid] = FREE;
		if (!m_lock_waitlist[lid]->empty()) {
			pthread_cond_signal(m_lock_waitlist[lid]->front());
			m_lock_waitlist[lid]->pop_front();
		}
	}
	pthread_mutex_unlock(&map_lock);
	return ret;
}

template<class T>
bool
lock_client_cache::listContains(std::list<T> l, T v) 
{
	for (typename std::list<T>::iterator it = l.begin(); it != l.end(); it++) {
		if (*it == v) {
			return true;
		}
	}
	return false;
} 

lock_protocol::status 
lock_client_cache::revoke(int clid, rlock_protocol::lockid_t lid, rlock_protocol::seqid_t seqid, int&) 
{
	printf("Revoke called for lock %016llx on client %d\n", lid, clid);
	struct timeval now;
	struct timespec timeout;
	lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&map_lock);
	while (m_lock_status[lid] == ACQUIRING) {
		printf("thread %016lx on client %d waits acquire to finish in revoke\n", pthread_self(), clid);
		gettimeofday(&now, NULL);
		timeout.tv_sec = now.tv_sec;
		timeout.tv_nsec = (now.tv_usec * 1000) + COND_TIMEOUT;
		pthread_cond_timedwait(&revoke_cond, &map_lock, &timeout);
		if (ret == ETIMEDOUT) {
			printf("revoke woke up by timeout\n");
		}
	}
	printf("thread %016lx on client %d no acquire running for lock %016llx\n", pthread_self(), clid, lid);
	
	if (m_lock_status[lid] == NONE || m_lock_status[lid] == RELEASING) {
		printf("lock already has been released or is releasing on client %d\n", clid);
		return ret;
	}
	// printf("Compare seqids: %016llx = %016llx\n", m_lock_seqid[lid], seqid);
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
		assert(false);
		m_waiting_revoke[lid] = seqid;
	}
	pthread_mutex_unlock(&map_lock);
	return ret;
}

lock_protocol::status
lock_client_cache::retry(int clid, rlock_protocol::lockid_t lid, int&)
{
	struct timeval now;
	struct timespec timeout;
	printf("Retry called for lock %016llx on client %d\n", lid, clid);
	lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&map_lock);
	if ( m_lock_waitlist[lid]->empty() || (m_lock_status[lid] == ACQUIRING) ) {
		m_lock_retry[lid] = true;
	} else {
		pthread_cond_t* cond = m_lock_waitlist[lid]->front();
		m_lock_waitlist[lid]->pop_front();
		//printf("client %d thread %016lx signal cond at %p\n", clid, pthread_self(), cond);
		// This is necessary because it is possible that the acquiring thread does not wait at the condition yet.
		while (!m_cond_waiting[cond]) {
			//printf("have to wait for condition\n");
			gettimeofday(&now, NULL);
			timeout.tv_sec = now.tv_sec;
			timeout.tv_nsec = (now.tv_usec * 1000) + COND_TIMEOUT;
			pthread_cond_timedwait(cond, &map_lock,&timeout);
		}
		pthread_cond_signal(cond);
	} //else {
		//printf("m_lock_waitlist[%016llx] is empty on client %d\n", lid, clid);
	//}
	pthread_mutex_unlock(&map_lock);
	return ret;
}