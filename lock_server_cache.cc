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
	assert(m_clid_rpcc.count(clid) != 0);
	printf("lock_server: Acquire lock %016llx for client %d\n", lid, clid);
	int ret = lock_protocol::OK;
	pthread_mutex_lock(&lock);
	if (m_clid_rpcc.count(clid) == 0) {
		// Client has not subscribed yet
		pthread_mutex_unlock(&lock);
		printf("lock_server:  Client %d has not subscribed yet.\n", clid);
		return lock_protocol::RPCERR;
	}
	if (m_lock_clid.count(lid) == 0) {
		// Lock is free. Assign it to client
		m_lock_clid[lid] = clid;
		m_lock_seqid[lid] = seqid;
		printf("Lock %016llx granted to client %d\n",lid, clid);
		if (m_lock_clid_count[lid].count(clid) == 0) {
			m_lock_clid_count[lid][clid] = 1; 
		} else {
			m_lock_clid_count[lid][clid]++; 
		}
	} else {
		// Lock is assigned to another client. Revoke it
		bool contains = false;
		for (std::list<lock_protocol::lockid_t>::iterator it = l_revoke.begin(); it != l_revoke.end(); it++) {
			if (*it == lid) {
				contains = true;
			}
		}
		if (!contains) {
			l_revoke.push_back(lid);
		}
		if (m_lock_retrylist.count(lid) == 0) {
			std::list<int>* l = new std::list<int>();
			m_lock_retrylist[lid] = l;
		}	
		printf("Lock %016llx NOT granted to client %d. Retry\n",lid, clid);
		m_lock_retrylist[lid]->push_back(clid);
		ret = lock_protocol::RETRY;
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
		printf("lock_server:  Client %d has not subscribed yet.\n", clid);
		return lock_protocol::RPCERR;
	}
	if (m_lock_clid[lid] != clid) {
		// This client does not hold the lock it wants to release. Error...
		pthread_mutex_unlock(&lock);
		printf("lock_server:  Client %d does not hold the lock %016llx.\n", clid, lid);
		return lock_protocol::RPCERR;
	}
	printf("Lock %016llx released by client %d\n", lid, clid);
	m_lock_clid.erase(lid);
	if (retry == 1) 
	{
		if (m_lock_retrylist.count(lid) == 0) {
			std::list<int>* l = new std::list<int>();
			m_lock_retrylist[lid] = l;
		}	
		printf("Add to retrylist: lock %016llx on client %d\n",lid, clid);
		m_lock_retrylist[lid]->push_back(clid);
	}
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
		// This lock has ever been acquired by the client
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
	int ret = -5;
	lock_protocol::lockid_t lid;
	rlock_protocol::seqid_t seqid;
	int r;
	while (true) {
		pthread_mutex_lock(&lock);
		while (l_revoke.empty()) {
			printf("l_revoke is empty\n");
			pthread_cond_wait(&revoke_cond, &lock);
			printf("revoker woke up\n");
		}
		lid = l_revoke.front();
		l_revoke.pop_front();
		if (m_lock_clid.count(lid) == 0) {
			printf("Can't revoke lock. Noone has it.\n");
			pthread_mutex_unlock(&lock);
			continue;
		}
		clid = m_lock_clid[lid];
		seqid = m_lock_seqid[lid];
		pthread_mutex_unlock(&lock);
		printf("Revoking %016llx on client %d and seqid %016llx\n", lid, clid, seqid);
		// if retry == true, the server notifies the client when lock lid is free again
		ret = m_clid_rpcc[clid]->call(rlock_protocol::revoke, clid, lid, seqid, r);
		printf("revoke ret = %d\n", ret);
		//pthread_mutex_lock(&lock);
		//pthread_mutex_unlock(&lock);
	}
  // This method should be a continuous loop, that sends revoke
  // messages to lock holders whenever another client wants the
  // same lock

}


// TODO: Maybe keeping the lock until all retries are dispatched improves performance (same for revoker)
void
lock_server_cache::retryer()
{
	int r;
	lock_protocol::lockid_t lid;
	int clid;
	int ret;
	std::map<lock_protocol::lockid_t, std::list<int>* >::iterator it;
	while (true) {
		pthread_mutex_lock(&lock);
		while (m_lock_retrylist.empty()) {
			printf("l_retry is empty.\n");
			pthread_cond_wait(&retry_cond, &lock);
			printf("Retryer woke up\n");
		}
		it = m_lock_retrylist.begin();
		//assert(it != NULL);
		lid = it->first;
		std::list<int>* l = it->second;
		assert(l != NULL);
		assert(!l->empty());
		clid = it->second->front();
		it->second->pop_front();
		if (it->second->empty()) {
			printf("List is empty now\n");
			m_lock_retrylist.erase(it);
			delete it->second;
		}
		pthread_mutex_unlock(&lock);
		// if retry == true, the server notifies the client when lock lid is free again
		printf("Calling retry for lock %016llx on client %d\n", lid, clid);
		ret = m_clid_rpcc[clid]->call(rlock_protocol::retry, clid, lid, r);
		printf("Call to client->retry() returned with ret = %d\n",ret);
		//pthread_mutex_lock(&lock);
		//pthread_mutex_unlock(&lock);
	}
  // This method should be a continuous loop, waiting for locks
  // to be released and then sending retry messages to those who
  // are waiting for it.

}



