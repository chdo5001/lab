#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"

	struct revoke_info {
		lock_protocol::lockid_t lid;
		int						clid;
		rlock_protocol::seqid_t	seqid;
	};
class lock_server_cache {

 public:
	enum lock_status { FREE, LOCKED, REVOKING };
  ~lock_server_cache();
  lock_server_cache();
  virtual lock_protocol::status stat(int clid, lock_protocol::lockid_t lid, int &);
  virtual lock_protocol::status acquire(int clid, lock_protocol::lockid_t lid, lock_protocol::seqid_t seqid, int &);
  virtual lock_protocol::status release(int clid, lock_protocol::lockid_t lid, int retry, int&);
  virtual lock_protocol::status subscribe(int clid, std::string host, int &);
  void revoker();
  void retryer();
    
  private:
	std::map<lock_protocol::status, lock_status> m_lock_status;
	std::map<int, rpcc*> m_clid_rpcc;
	// Maps locks to the client holding it
	std::map<lock_protocol::lockid_t, int> m_lock_clid;
	// Maps locks to a list of clients waiting for it
	std::map<lock_protocol::lockid_t, std::list<int>* > m_lock_retrylist;
	// List of locks that shall be revoked by the revoker
	std::list<revoke_info> l_revoke;
	// Keeps track of how many times a lock was acquired by a specific client
	std::map<lock_protocol::lockid_t, std::map<int, int> > m_lock_clid_count;
	// Maps the lock to the seqid of the message that acquired it
	std::map<lock_protocol::lockid_t, rlock_protocol::seqid_t> m_lock_seqid;
	// Mutex protecting shared data structures of the class
	pthread_mutex_t lock;
	// Conditions on which the revoker and retryer wait
	pthread_cond_t revoke_cond;
	pthread_cond_t retry_cond;
	bool retryer_ready;
	bool revoker_ready;
	std::list<lock_protocol::lockid_t> l_released;
};

#endif
