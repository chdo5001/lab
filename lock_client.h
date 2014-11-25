// lock client interface.

#ifndef lock_client_h
#define lock_client_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"
#include <vector>
#include <map>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>

// Client interface to the lock server
class lock_client {
  private:
	std::map<int, unsigned int> lock_tid;
	// Mutex protecting the above map
	pthread_mutex_t map_lock;
	// Condition variable clients wait on for a lock to be released
	pthread_cond_t lock_free;
  protected:
	rpcc *cl;
  public:
	lock_client(std::string d);
	virtual ~lock_client();
	virtual lock_protocol::status acquire(lock_protocol::lockid_t);
	virtual lock_protocol::status release(lock_protocol::lockid_t);
	virtual lock_protocol::status stat(lock_protocol::lockid_t);
};


#endif 
