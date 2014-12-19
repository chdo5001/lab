// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

static void *
revokethread(void *x)
{
  lock_server_cache *sc = (lock_server_cache *) x;
  sc->revoker();
  return 0;
}

static void *
retrythread(void *x)
{
  lock_server_cache *sc = (lock_server_cache *) x;
  sc->retryer();
  return 0;
}

lock_server_cache::lock_server_cache()
{
  revoker_ready = false;
  retryer_ready = false;
  pthread_t th;
  int r = pthread_create(&th, NULL, &revokethread, (void *) this);
  assert (r == 0);
  r = pthread_create(&th, NULL, &retrythread, (void *) this);
  assert (r == 0);
  pthread_mutex_init(&lock, NULL);
  pthread_cond_init(&revoke_cond, NULL);
  pthread_cond_init(&retry_cond, NULL);
}

lock_server_cache::~lock_server_cache()
{
  pthread_mutex_destroy(&lock);
  pthread_cond_init(&revoke_cond, NULL);
  pthread_cond_init(&retry_cond, NULL);
}

// TODO: Theoretisch sollte es noch irgendwas geben, dass die Map akutell hält (unsubscribe() etc).
// Da der Server aber nur kurz laufen dürfte, können wir uns das wohl sparen.
lock_protocol::status
lock_server_cache::subscribe(int clid, std::string host, int &)
{
	sockaddr_in dstsock;
	make_sockaddr(host.c_str(), &dstsock);
	rpcc* cl = new rpcc(dstsock);
	int r = cl->bind();
	if (r == 0) {
		printf("lock_server: bind successful\n");
	} else {
		printf("lock_server: bind failed %d\n", r);
		exit(0);
	}
	pthread_mutex_lock(&lock);
	m_clid_rpcc[clid] = cl;
	pthread_mutex_unlock(&lock);
	return lock_protocol::OK;
}

lock_protocol::status
lock_server_cache::acquire(int clid, lock_protocol::lockid_t lid, lock_protocol::seqid_t seqid, int &) 
{
	if (m_lock_status.count(lid) == 0) {
		// Never saw this lock before. Create a status map entry for it
		m_lock_status[lid] = FREE;
	}
	assert(m_clid_rpcc.count(clid) != 0);
	//printf("lock_server: Try to acquire lock %016llx for client %d\n", lid, clid);
	int ret = lock_protocol::OK;
	pthread_mutex_lock(&lock);
	if (m_clid_rpcc.count(clid) == 0) {
		// Client has not subscribed yet
		pthread_mutex_unlock(&lock);
		//printf("lock_server:  Client %d has not subscribed yet.\n", clid);
		return lock_protocol::RPCERR;
	}
	if (m_lock_clid[lid] == FREE) {
		// Lock is free. Assign it to client
		m_lock_clid[lid] = clid;
		m_lock_seqid[lid] = seqid;
		m_lock_status[lid] = LOCKED;
		//printf("Lock %016llx granted to client %d\n",lid, clid);
		// Update acquired-count
		if (m_lock_clid_count[lid].count(clid) == 0) {
			m_lock_clid_count[lid][clid] = 1; 
		} else {
			m_lock_clid_count[lid][clid]++; 
		}
		// If there are other clients already waiting for the lock, immediately revoke it again
		if (m_lock_retrylist.count(lid) != 0) {
			if (m_lock_status[lid] != REVOKING) {
				m_lock_status[lid] = REVOKING;
				revoke_info info;
				info.lid = lid;
				info.clid = m_lock_clid[lid];
				info.seqid = m_lock_seqid[lid];
				//printf("Add %016llx to revokelist immediately\n", lid);
				l_revoke.push_back(info);
			}
			if (!revoker_ready) {
				pthread_cond_wait(&revoke_cond, &lock);
			}
			pthread_cond_signal(&revoke_cond);
		}
	} else {
		// Lock is assigned to another client. Revoke it
		/*
		bool contains = false;
		for (std::list<lock_protocol::lockid_t>::iterator it = l_revoke.begin(); it != l_revoke.end(); it++) {
			if (*it == lid) {
				contains = true;
			}
		}
		if (!contains) {
			l_revoke.push_back(lid);
		}*/
		// Sending one revoke-request is sufficient
		if (m_lock_status[lid] != REVOKING) {
			m_lock_status[lid] = REVOKING;
			revoke_info info;
			info.lid = lid;
			info.clid = m_lock_clid[lid];
			info.seqid = m_lock_seqid[lid];
			//printf("Add %016llx to revokelist\n", lid);
			l_revoke.push_back(info);
		}
		if (m_lock_retrylist.count(lid) == 0) {
			std::list<int>* l = new std::list<int>();
			m_lock_retrylist[lid] = l;
		}	
		//printf("Lock %016llx NOT granted to client %d. Retry\n",lid, clid);
		m_lock_retrylist[lid]->push_back(clid);
		ret = lock_protocol::RETRY;
		if (!revoker_ready) {
			pthread_cond_wait(&revoke_cond, &lock);
		}
		pthread_cond_signal(&revoke_cond);
	}
	pthread_mutex_unlock(&lock);
	return ret;
}

lock_protocol::status
lock_server_cache::release(int clid, lock_protocol::lockid_t lid, int retry, int&) {
	assert(m_clid_rpcc.count(clid) != 0);
	int ret = lock_protocol::OK;
	pthread_mutex_lock(&lock);
	if (m_clid_rpcc.count(clid) == 0) {
		// Client has not subscribed yet
		pthread_mutex_unlock(&lock);
		//printf("lock_server:  Client %d has not subscribed yet.\n", clid);
		return lock_protocol::RPCERR;
	}
	if (m_lock_clid[lid] != clid) {
		// This client does not hold the lock it wants to release. Error...
		pthread_mutex_unlock(&lock);
		//printf("lock_server:  Client %d does not hold the lock %016llx.\n", clid, lid);
		return lock_protocol::RPCERR;
	}
	printf("Lock %016llx released by client %d\n", lid, clid);
	m_lock_clid.erase(lid);
	m_lock_seqid.erase(lid);
	m_lock_status[lid] = FREE;
	if (retry == 1) 
	{
		// The releasing client wants the lock again
		if (m_lock_retrylist.count(lid) == 0) {
			std::list<int>* l = new std::list<int>();
			m_lock_retrylist[lid] = l;
		}	
		//printf("Add to retrylist: lock %016llx on client %d\n",lid, clid);
		m_lock_retrylist[lid]->push_back(clid);
	}
	while (!retryer_ready) {
		pthread_cond_wait(&retry_cond, &lock);
	}
	l_released.push_back(lid);
	pthread_cond_signal(&retry_cond);
	pthread_mutex_unlock(&lock);
	return ret;
}

lock_protocol::status
lock_server_cache::stat(int clid, lock_protocol::lockid_t lid, int& r)
{
	assert(m_clid_rpcc.count(clid) != 0);
	pthread_mutex_lock(&lock);
	if (m_lock_clid_count[lid].count(clid) == 0) {
		// This lock has never been acquired by the client
		r = 0;
	} else {
		r = m_lock_clid_count[lid][clid];
	}
	pthread_mutex_unlock(&lock);
	return lock_protocol::OK;
}

void
lock_server_cache::revoker()
{
	//rlock_protocol::lockid_t lid;
	int clid;
	//int ret;
	revoke_info info;
	lock_protocol::lockid_t lid;
	rlock_protocol::seqid_t seqid;
	int r;
	while (true) {
		pthread_mutex_lock(&lock);
		while (l_revoke.empty()) {
			//printf("l_revoke is empty\n");
			revoker_ready = true;
			pthread_cond_signal(&revoke_cond);
			pthread_cond_wait(&revoke_cond, &lock);
			revoker_ready = false;
			//printf("revoker woke up\n");
		}
		info = l_revoke.front();
		l_revoke.pop_front();
		lid = info.lid;
		clid = info.clid;
		seqid = info.seqid;
		// Check if the data of the lock has changed meanwhile
		if (m_lock_clid.count(lid) == 0) {
			//printf("Can't revoke lock. Noone has it.\n");
			pthread_mutex_unlock(&lock);
			continue;
		}
		if ( (m_lock_clid[lid] != clid) || (m_lock_seqid[lid] != seqid) ) {
			pthread_mutex_unlock(&lock);
			continue;
		}
		pthread_mutex_unlock(&lock);
		//printf("Revoking %016llx on client %d and seqid %016llx\n", lid, clid, seqid);
		// if retry == true, the server notifies the client when lock lid is free again
		//ret = 
		m_clid_rpcc[clid]->call(rlock_protocol::revoke, clid, lid, seqid, r);
		//printf("revoke ret = %d\n", ret);
		//pthread_mutex_lock(&lock);
		//pthread_mutex_unlock(&lock);
	}
  // This method should be a continuous loop, that sends revoke
  // messages to lock holders whenever another client wants the
  // same lock

}


void
lock_server_cache::retryer()
{
	int r;
	lock_protocol::lockid_t lid;
	int clid;
	//int ret;
	//bool free = false;
	std::list<int>* l = NULL;
	std::map<lock_protocol::lockid_t, int>::iterator it;
	while (true) {
		pthread_mutex_lock(&lock);
		//while (!l_released.empty()) {
			//printf("l_retry is empty.\n");
			retryer_ready = true;
			pthread_cond_signal(&retry_cond);
			pthread_cond_wait(&retry_cond, &lock);
			retryer_ready = false;
		//}
		std::map<lock_protocol::lockid_t, int> m;
		
		while(!l_released.empty()) {
			lid = l_released.front();
			l_released.pop_front();
			assert(!m_lock_retrylist[lid]->empty());
			l = m_lock_retrylist[lid];
			m[lid] = l->front();
			l->pop_front();
			if (l->empty()) {
			//printf("List is empty now\n");
				m_lock_retrylist.erase(lid);
				delete l;
			}
		}

		pthread_mutex_unlock(&lock);
		// if retry == true, the server notifies the client when lock lid is free again
		for (it = m.begin(); it != m.end(); it++) {
			lid = it->first;
			clid = it->second;
			//printf("Calling retry for lock %016llx on client %d\n", lid, clid);
			//ret = 
			m_clid_rpcc[clid]->call(rlock_protocol::retry, clid, lid, r);
		}
		m.clear();
		//printf("Call to client->retry() returned with ret = %d\n",ret);
		//pthread_mutex_lock(&lock);
		//pthread_mutex_unlock(&lock);
	}
  // This method should be a continuous loop, waiting for locks
  // to be released and then sending retry messages to those who
  // are waiting for it.

}



