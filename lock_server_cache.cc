// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "rpc/marshall.h"

#define COND_TIMEOUT 100000

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

lock_server_cache::lock_server_cache(class rsm *_rsm) 
  : rsm (_rsm)
{
  pthread_t th;
  pthread_mutex_init(&lock, NULL);
  pthread_cond_init(&revoke_cond, NULL);
  pthread_cond_init(&retry_cond, NULL);
  rsm->set_state_transfer(this);
  int r = pthread_create(&th, NULL, &revokethread, (void *) this);
  assert (r == 0);
  r = pthread_create(&th, NULL, &retrythread, (void *) this);
  assert (r == 0);
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
	printf("Subscribe\n");
	m_clid_host[clid] = host;
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
	printf("Acquire\n");
	if (m_lock_status.count(lid) == 0) {
		// Never saw this lock before. Create a map entry for it
		m_lock_status[lid] = FREE;
		std::vector<int> v;
		//m_lock_retrylist[lid] = new std::list<int>();
		m_lock_retrylist[lid] = v;
		m_lock_seqid[lid] = -1;
		m_lock_clid[lid] = -1;
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
	
	if (m_lock_status[lid] == FREE) {
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
		if (!m_lock_retrylist[lid].empty()) {
			if (m_lock_status[lid] != REVOKING) {
				m_lock_status[lid] = REVOKING;
				revoke_info info;
				info.lid = lid;
				info.clid = m_lock_clid[lid];
				info.seqid = m_lock_seqid[lid];
				//printf("Add %016llx to revokelist immediately\n", lid);
				l_revoke.push_back(info);
			}
			pthread_cond_signal(&revoke_cond);
		}
	} else {
		// Lock is assigned to another client. Revoke it
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
		//printf("Lock %016llx NOT granted to client %d.\n",lid, clid);
		m_lock_retrylist[lid].push_back(clid);
		ret = lock_protocol::RETRY;
		pthread_cond_signal(&revoke_cond);
	}
	pthread_mutex_unlock(&lock);
	return ret;
}

lock_protocol::status
lock_server_cache::release(int clid, lock_protocol::lockid_t lid, int retry, int&) {
	printf("Release\n");
	// if retry == true, the server notifies the client when lock lid is free again
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
		//printf("lock_server:  Client %d does not hold the lock %016llx.\n", clid, lid);
		return lock_protocol::RPCERR;
	}
	//printf("Lock %016llx released by client %d\n", lid, clid);
	m_lock_clid[lid] = -1;
	m_lock_seqid[lid] = -1;
	m_lock_status[lid] = FREE;
	if (retry == 1) 
	{
		// The releasing client wants the lock again
		m_lock_retrylist[lid].push_back(clid);
	}
	pthread_cond_signal(&retry_cond);
	pthread_mutex_unlock(&lock);
	return ret;
}

lock_protocol::status
lock_server_cache::stat(int clid, lock_protocol::lockid_t lid, int& r)
{
	printf("Stat\n");
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
	int clid;
	//int ret;
	revoke_info info;
	lock_protocol::lockid_t lid;
	rlock_protocol::seqid_t seqid;
	int r;
	//int ret;
	struct timeval now;
	struct timespec timeout;
	while (true) {
		pthread_mutex_lock(&lock);
		while (l_revoke.empty()) {
			//printf("l_revoke is empty\n");
			gettimeofday(&now, NULL);
			timeout.tv_sec = now.tv_sec;
			timeout.tv_nsec = (now.tv_usec * 1000) + COND_TIMEOUT;
			pthread_cond_timedwait(&revoke_cond, &lock, &timeout);
			//printf("revoker woke up\n");
		}
		info = l_revoke.front();
		//l_revoke.pop_front();
		l_revoke.erase(l_revoke.begin());
		lid = info.lid;
		clid = info.clid;
		seqid = info.seqid;
		// Check if the data of the lock has changed (e.g. it may be released and reacquired by another client meanwhile)
		if (m_lock_clid[lid] == FREE) {
			//printf("Can't revoke lock. Noone has it.\n");
			pthread_mutex_unlock(&lock);
			continue;
		}
		if ( (m_lock_clid[lid] != clid) || (m_lock_seqid[lid] != seqid) ) {
			// Data of the lock has changed. Depricated revoke request
			pthread_mutex_unlock(&lock);
			continue;
		}
		pthread_mutex_unlock(&lock);
		//printf("Revoking %016llx on client %d and seqid %016llx\n", lid, clid, seqid);
		if (rsm->amiprimary()) {
			//printf("I'm primary. CALL REVOKE\n");
			int ret = m_clid_rpcc[clid]->call(rlock_protocol::revoke, clid, lid, seqid, r);
			assert (ret >= 0);
		}
	}
}


void
lock_server_cache::retryer()
{
	int r;
	lock_protocol::lockid_t lid;
	int clid;
		struct timeval now;
	struct timespec timeout;
	//int ret;
	std::map<lock_protocol::lockid_t, int> m;
	std::list<lock_protocol::lockid_t> free_locks;
	std::map<lock_protocol::lockid_t, int>::iterator it;
	while (true) {
		pthread_mutex_lock(&lock);
		while (free_locks.empty()) {
			gettimeofday(&now, NULL);
			timeout.tv_sec = now.tv_sec;
			timeout.tv_nsec = (now.tv_usec * 1000) + COND_TIMEOUT;
			pthread_cond_timedwait(&retry_cond, &lock, &timeout);
			findFreeLocks(free_locks);
		}
		
		while(!free_locks.empty()) {
			lid = free_locks.front();
			free_locks.pop_front();
			//assert(!m_lock_retrylist[lid]->empty());
			if (!m_lock_retrylist[lid].empty()) {
				m[lid] = m_lock_retrylist[lid].front();
				//m_lock_retrylist[lid].pop_front();
				m_lock_retrylist[lid].erase(m_lock_retrylist[lid].begin());
			}
		}

		pthread_mutex_unlock(&lock);
		// if retry == true, the server notifies the client when lock lid is free again
		for (it = m.begin(); it != m.end(); it++) {
			lid = it->first;
			clid = it->second;
			//printf("Calling retry for lock %016llx on client %d\n", lid, clid);
			//ret = 
			if (rsm->amiprimary()) {
				//printf("I'm primary. CALL RETRY\n");
				int ret = m_clid_rpcc[clid]->call(rlock_protocol::retry, clid, lid, r);
				assert (ret >= 0);
			}
		}
		m.clear();
	}
}

void
lock_server_cache::findFreeLocks(std::list<lock_protocol::lockid_t>& l_free)
{
	// Is only called from retryer when it holds the mutex. Thus, we don't have to do this here
	//std::map<lock_protocol::lockid_t, lock_status>::iterator it;
	std::map<lock_protocol::lockid_t, int>::iterator it;
	for (it = m_lock_status.begin(); it != m_lock_status.end(); it++) {
		if (it->second == FREE) {
			l_free.push_back(it->first);
		}
	}
}

 std::string lock_server_cache::marshal_state() 
 {
	printf("lock_server_cache::marshal_state\n");
	pthread_mutex_lock(&lock);
	marshall rep;
	/*** std::map<lock_protocol::lockid_t, lock_status> m_lock_status;  ***/
	rep << m_lock_status;
	
	/*** std::map<int, std::string> m_clid_host; ***/
	rep << m_clid_host;
	
	// Maps locks to the client holding it
	/*** std::map<lock_protocol::lockid_t, int> m_lock_clid; ***/
	rep << m_lock_clid;
	
	// Maps locks to a list of clients waiting for it
	/*** std::map<lock_protocol::lockid_t, std::vector<int> > m_lock_retrylist; ***/
		
	std::map<lock_protocol::lockid_t, std::vector<int> >::iterator it;
	rep << m_lock_retrylist.size();
	for (it = m_lock_retrylist.begin(); it != m_lock_retrylist.end(); it++) {
		long long unsigned int name = it->first;
		std::vector<int> vec = m_lock_retrylist[name];
		rep << name;
		rep << vec;
	}
	// List of locks that shall be revoked by the revoker
	/*** std::vector<revoke_info> l_revoke; ***/
	//rep << l_revoke;
	rep << l_revoke.size();
	std::vector<revoke_info>::iterator i;
	for (i = l_revoke.begin(); i != l_revoke.end(); i++) {
		rep << i->lid;
		rep <<	i->clid;
		rep << i->seqid;
	}
	
	// Keeps track of how many times a lock was acquired by a specific client
	/*** std::map<lock_protocol::lockid_t, std::map<int, int> > m_lock_clid_count; ***/
	rep << m_lock_clid_count.size();
	std::map<lock_protocol::lockid_t, std::map<int, int> >::iterator iter_lock;
	for (iter_lock = m_lock_clid_count.begin(); iter_lock != m_lock_clid_count.end(); iter_lock++) {
		long long unsigned int name = iter_lock->first;
		std::map<int, int> m = m_lock_clid_count[name];
		rep << name;
		rep << m;
	}
	// Maps the lock to the seqid of the message that acquired it
	/*** std::map<lock_protocol::lockid_t, rlock_protocol::seqid_t> m_lock_seqid; ***/
	rep << m_lock_seqid;
	pthread_mutex_unlock(&lock);
	printf("lock_server_cache::marshal_state DONE\n");
	return rep.str();
 }

 void lock_server_cache::unmarshal_state(std::string state)
{
	printf("lock_server_cache::unmarshal_state\n");
	pthread_mutex_lock(&lock);
	unmarshall rep(state);
	/*** std::map<lock_protocol::lockid_t, lock_status> m_lock_status;  ***/
	rep >> m_lock_status;
	
	/*** std::map<int, std::string> m_clid_host; ***/
	rep >> m_clid_host;
	printf("m_clid_host.size() = %d\n", m_clid_host.size());
	// rebuild m_clid_rpcc
	int dummy;
	for (std::map<int, std::string>::iterator m_it = m_clid_host.begin(); m_it != m_clid_host.end(); m_it++) {
	  printf("subscribe(%d, %s)\n", m_it->first, (m_it->second).c_str());
	  //subscribe(m_it->first, m_it->second, dummy);
	  sockaddr_in dstsock;
	  make_sockaddr((m_it->second).c_str(), &dstsock);
	  rpcc* cl = new rpcc(dstsock);
	  int r = cl->bind();
	  if (r == 0) {
		printf("lock_server: bind successful\n");
	  } else {
		printf("lock_server: bind failed %d\n", r);
		exit(0);
	  }
	  m_clid_rpcc[m_it->first] = cl;
	  printf("Subscribe Done\n");
	}
	printf("Rebuild of m_clid_rpcc (size: %d) done\n", m_clid_rpcc.size());
	//std::map<int, rpcc*> m_clid_rpcc;
	// Maps locks to the client holding it
	/*** std::map<lock_protocol::lockid_t, int> m_lock_clid; ***/
	rep >> m_lock_clid;
	
	// Maps locks to a list of clients waiting for it
	/*** std::map<lock_protocol::lockid_t, std::vector<int> > m_lock_retrylist; ***/
		
	//std::map<lock_protocol::lockid_t, std::vector<int> >::iterator it;
	unsigned int size;
	rep >> size;
	for (unsigned int i = 0; i < size; i++) {
		long long unsigned int name;
		rep >> name;
		std::vector<int> vec;
		rep >> vec;
		m_lock_retrylist[name] = vec;
	}
	// List of locks that shall be revoked by the revoker
	/*** std::vector<revoke_info> l_revoke; ***/
	//rep << l_revoke;
	rep >> size;
	for (unsigned int i = 0; i < size; i++) {
		revoke_info info;
		rep >> info.lid;
		rep >>	info.clid;
		rep >> info.seqid;
		l_revoke.push_back(info);
	}
	
	// Keeps track of how many times a lock was acquired by a specific client
	/*** std::map<lock_protocol::lockid_t, std::map<int, int> > m_lock_clid_count; ***/
	rep >> size;
	for (unsigned int i = 0; i < size; i++) {
		long long unsigned int name;
		rep >> name;
		std::map<int, int> m;
		rep >> m;
		m_lock_clid_count[name] = m;
	}
	
	// Maps the lock to the seqid of the message that acquired it
	/*** std::map<lock_protocol::lockid_t, rlock_protocol::seqid_t> m_lock_seqid; ***/
	rep >> m_lock_seqid;
	pthread_mutex_unlock(&lock);
	printf("lock_server_cache::unmarshal_state DONE\n");
}
