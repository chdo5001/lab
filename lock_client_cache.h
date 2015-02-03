// lock client interface.

#ifndef lock_client_cache_h

#define lock_client_cache_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_client.h"
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include "rsm_client.h"

// Classes that inherit lock_release_user can override dorelease so that 
// that they will be called when lock_client releases a lock.
// You will not need to do anything with this class until Lab 6.
class lock_release_user {
 public:
  virtual void dorelease(lock_protocol::lockid_t) = 0;
  virtual ~lock_release_user() {};
};


// SUGGESTED LOCK CACHING IMPLEMENTATION PLAN:
//
// to work correctly for lab 7,  all the requests on the server run till 
// completion and threads wait on condition variables on the client to
// wait for a lock.  this allows the server to be replicated using the
// replicated state machine approach.
//
// On the client a lock can be in several states:
//  - free: client owns the lock and no thread has it
//  - locked: client owns the lock and a thread has it
//  - acquiring: the client is acquiring ownership
//  - releasing: the client is releasing ownership
//
// in the state acquiring and locked there may be several threads
// waiting for the lock, but the first thread in the list interacts
// with the server and wakes up the threads when its done (released
// the lock).  a thread in the list is identified by its thread id
// (tid).
//
// a thread is in charge of getting a lock: if the server cannot grant
// it the lock, the thread will receive a retry reply.  at some point
// later, the server sends the thread a retry RPC, encouraging the client
// thread to ask for the lock again.
//
// once a thread has acquired a lock, its client obtains ownership of
// the lock. the client can grant the lock to other threads on the client 
// without interacting with the server. 
//
// the server must send the client a revoke request to get the lock back. this
// request tells the client to send the lock back to the
// server when the lock is released or right now if no thread on the
// client is holding the lock.  when receiving a revoke request, the
// client adds it to a list and wakes up a releaser thread, which returns
// the lock the server as soon it is free.
//
// the releasing is done in a separate a thread to avoid
// deadlocks and to ensure that revoke and retry RPCs from the server
// run to completion (i.e., the revoke RPC cannot do the release when
// the lock is free.
//
// a challenge in the implementation is that retry and revoke requests
// can be out of order with the acquire and release requests.  that
// is, a client may receive a revoke request before it has received
// the positive acknowledgement on its acquire request.  similarly, a
// client may receive a retry before it has received a response on its
// initial acquire request.  a flag field is used to record if a retry
// has been received.
//


class lock_client_cache : public lock_client {
	enum lock_status { NONE, FREE, LOCKED, RETRYING, ACQUIRING, RELEASING };
	//typedef int l_status;
 private:
	//rsm_client* cl;
  class lock_release_user *lu;
  int rlock_port;
  std::string hostname;
  std::string id;
	rlock_protocol::seqid_t seqid;
	// List containing revoke requests for locks (Will be used for received revokes msg before the acqu msg later)
	//std::list<lock_protocol::lockit_t> l_revoke_locks;
  	// Maps locks to the thread holding it
	std::map<rlock_protocol::lockid_t, pthread_t> m_lock_thread;
	// Maps locks to their status
	std::map<rlock_protocol::lockid_t, lock_status> m_lock_status;
	// Maps locks to a list of threads waiting to acquire it
	std::map<rlock_protocol::lockid_t, std::list<pthread_cond_t*>* > m_lock_waitlist;
	// Maps locks to sequence number of rpc that acquired it
	std::map<rlock_protocol::lockid_t, rlock_protocol::seqid_t> m_lock_seqid;
	// If there is an entry for a condition then a thread waits on it
	std::map<pthread_cond_t*, bool> m_cond_waiting;
	std::map<lock_protocol::lockid_t, bool> m_lock_retry;
	// Mutex protecting shared data structures of the class
	pthread_mutex_t map_lock;
	// Condition variable releaser() waits on for revoked locks to be freed
	pthread_cond_t lock_free;
	pthread_cond_t revoke_cond;
	//pthread_mutex_t release_lock;
	std::list<rlock_protocol::lockid_t> l_revoke;
	std::map<rlock_protocol::lockid_t, rlock_protocol::seqid_t> m_waiting_revoke;
	std::list<rlock_protocol::lockid_t> l_released;
	bool wait_for_revoke_cond;
	template<typename T> bool listContains(std::list<T> l, T v); 
 public:

	
	lock_protocol::status revoke(int clid, rlock_protocol::lockid_t lid, rlock_protocol::seqid_t seqid, int&);
	lock_protocol::status retry(int clid, rlock_protocol::lockid_t lid, int&);
  static int last_port;
  lock_client_cache(std::string xdst, class lock_release_user *l = 0);
  virtual ~lock_client_cache();
  lock_protocol::status acquire(rlock_protocol::lockid_t);
  virtual lock_protocol::status release(rlock_protocol::lockid_t);
  void releaser();
};
#endif

