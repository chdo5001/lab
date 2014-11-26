// this is the lock server
// the lock client has a similar interface

#ifndef lock_server_h
#define lock_server_h

#include <string>
#include <map>
#include <pthread.h>

#include "lock_protocol.h"
#include "lock_client.h"
#include "rpc.h"


class lock_server {
 private:
	// Maps locks to the client holding it
	std::map<lock_protocol::lockid_t, int> lock_client;
	// Maps locks to the thread holding it
	std::map<lock_protocol::lockid_t, unsigned int> lock_thread;
	// Tracks how often a client has acquired a specific lock
	std::map<int, std::map<lock_protocol::lockid_t, int> > clt_lock_count;
	// Mutex protecting the above maps
	pthread_mutex_t map_lock;
	// Condition variable clients wait on for a lock to be released
	pthread_cond_t lock_free;
 
 protected:
  //int nacquire;

 public:
  lock_server();
  ~lock_server();
  lock_protocol::status stat(int clid, lock_protocol::lockid_t lid, int &r);
  lock_protocol::status acquire(int clid, unsigned int tid, lock_protocol::lockid_t lid, int &);
  lock_protocol::status release(int clid, unsigned int tid, lock_protocol::lockid_t lid, int &);
};

#endif 







