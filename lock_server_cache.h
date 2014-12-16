#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"



class lock_server_cache {
 public:

  lock_server_cache();
  //virtual lock_protocol::status stat(lock_protocol::lockid_t, int &){};
  //virtual lock_protocol::status acquire(lock_protocol::lockid_t, int &){};
 // virtual lock_protocol::status release(lock_protocol::lockid_t, int &){};
  virtual lock_protocol::status subscribe(lock_protocol::lockid_t, int &){return lock_protocol::OK;};
  void revoker();
  void retryer();
  

	
};

#endif
