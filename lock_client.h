// lock client interface.

#ifndef lock_client_h
#define lock_client_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"
#include "rsm_client.h"
#include <vector>
#include <map>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>

// Client interface to the lock server
class lock_client {
  protected:
	rsm_client *cl;
  public:
	lock_client(std::string d);
	virtual ~lock_client();
	virtual lock_protocol::status acquire(lock_protocol::lockid_t);
	virtual lock_protocol::status release(lock_protocol::lockid_t);
	virtual lock_protocol::status stat(lock_protocol::lockid_t);
};


#endif 
