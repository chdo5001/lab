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
}

lock_client_cache::~lock_client_cache() {
	pthread_mutex_destroy(&map_lock);
	pthread_cond_destroy(&lock_free);
}

void
lock_client_cache::releaser()
{

  // This method should be a continuous loop, waiting to be notified of
  // freed locks that have been revoked by the server, so that it can
  // send a release RPC.
	rlock_protocol::lockid_t lid;
	int r;
	int retry;
	while (true) {
		pthread_mutex_lock(&map_lock);
		while (l_released.empty()) {
			pthread_cond_wait(&lock_free, &map_lock);
		}
		lid = l_released.front();
		l_released.pop_front();
		if (m_lock_waitlist[lid].empty()) {
			retry = 0;
		} else {
			retry = 1;
		}
		pthread_mutex_unlock(&map_lock);
		// if retry == 1, the server notifies this client when lock lid is free again
		cl->call(lock_protocol::release, cl->id(), lid, retry, r);
		pthread_mutex_lock(&map_lock);
		m_lock_status.erase(lid);
		m_lock_seqid.erase(lid);
		pthread_mutex_unlock(&map_lock);
	}

}


lock_protocol::status
lock_client_cache::acquire(rlock_protocol::lockid_t lid)
{
	lock_protocol::status ret = lock_protocol::OK;
	int r;
	pthread_mutex_lock(&map_lock);
	// The lock is not hold by the client. Try to acquire it.
	if (m_lock_status.count(lid) == 0) {
		m_lock_status[lid] = ACQUIRING;
		assert(m_lock_waitlist[lid].empty()); 
		pthread_cond_t* cond = new pthread_cond_t();
		pthread_cond_init(cond, NULL);
		// The acquiring thread should be the first in the waitlist. I think this is not necessary, but just fair.
		m_lock_waitlist[lid].push_front(cond);
		seqid++;
		m_lock_seqid[lid] = seqid;
		pthread_mutex_unlock(&map_lock);
		ret = cl->call(lock_protocol::acquire, cl->id(), lid, seqid, r);
		if (ret == lock_protocol::OK) {
			// Successful
			pthread_mutex_lock(&map_lock);
			assert(m_lock_waitlist[lid].front() == cond);
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
			return lock_protocol::OK;
		}
		if (ret == lock_protocol::RETRY) {
			// Unsuccessful. Retry later
			pthread_mutex_lock(&map_lock);
			while(m_lock_status[lid] != FREE) {
				pthread_cond_wait(cond, &map_lock);
			}
			assert(m_lock_waitlist[lid].front() == cond);
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
		}
		return ret;
	}
	
		// If the calling thread already holds this lock just return
	if (m_lock_thread.count(lid) != 0) {
		if (pthread_equal(pthread_self(), m_lock_thread[lid]) != 0) {
			pthread_mutex_unlock(&map_lock);
			return ret;
		}
	}
	
	
	// If the lock is FREE, assign it to the calling thread
	if (m_lock_status[lid] == FREE) {
		m_lock_status[lid] = LOCKED;
		m_lock_thread[lid] = pthread_self();
		pthread_mutex_unlock(&map_lock);
		return ret;
	}
	
	// If the client holds the lock, but it is locked/releasing/acquiring, wait for it
	if (m_lock_status[lid] > 1) {
		pthread_cond_t* cond = new pthread_cond_t();
		pthread_cond_init(cond, NULL);
		m_lock_waitlist[lid].push_back(cond);
		// TODO: Same as above. Maybe move in own method.
		while(m_lock_status[lid] != FREE) {
				pthread_cond_wait(cond, &map_lock);
			}
		assert(m_lock_waitlist[lid].front() == cond);
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
	}
	
	pthread_mutex_unlock(&map_lock);
	return ret;
}

lock_protocol::status
lock_client_cache::release(rlock_protocol::lockid_t lid)
{
	lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&map_lock);
	// Does this thread really holds the lock?
	if (m_lock_thread.count(lid) != 0) {
		if (!(m_lock_thread[lid] != pthread_self())) {
			pthread_mutex_unlock(&map_lock);
			return ret;
		}
	}
	m_lock_thread.erase(lid);
	bool contains = false;
	for (std::list<rlock_protocol::lockid_t>::iterator it = l_revoke.begin(); it != l_revoke.end(); it++) {
		if (*it == lid) {
			contains = true;
			break;
		}
	}
	if (contains) {
		m_lock_status[lid] = RELEASING;
		l_released.push_back(lid);
		l_revoke.remove(lid);
		pthread_cond_signal(&lock_free);
		pthread_mutex_unlock(&map_lock);
		return ret;
	} else {
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
	lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&map_lock);
	assert(m_lock_seqid.count(lid) != 0);
	if (m_lock_seqid[lid] == seqid) {
		l_revoke.push_back(lid);
	}
	if (m_lock_seqid[lid] < seqid) {
		m_waiting_revoke[lid] = seqid;
	}
	pthread_mutex_unlock(&map_lock);
	return ret;
}

lock_protocol::status
lock_client_cache::retry(int clid, rlock_protocol::lockid_t lid, int&)
{
	lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&map_lock);
	if (!m_lock_waitlist[lid].empty()) {
		pthread_cond_signal(m_lock_waitlist[lid].front());
	}
	pthread_mutex_unlock(&map_lock);
	return ret;
}