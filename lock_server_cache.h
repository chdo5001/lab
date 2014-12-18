#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"



class lock_server_cache {
 public:

  lock_server_cache();
  //virtual lock_protocol::status stat(int clid, lock_protocol::lockid_t lid, int &){};
  //virtual lock_protocol::status acquire(int clid, lock_protocol::lockid_t lid, int &){};
 // virtual lock_protocol::status release(int clid, lock_protocol::lockid_t lid, int &){};
  virtual lock_protocol::status subscribe(int clid, std::string host, int &);
  void revoker();
  void retryer();
  
  private:
	std::map<int, rpcc*> m_clid_rpcc;
	
	
};

#endif
