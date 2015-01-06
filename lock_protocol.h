<<<<<<< HEAD
//<<<<<<< HEAD
// lock protocol

#ifndef lock_protocol_h
#define lock_protocol_h

#include "rpc.h"

class lock_protocol {
 public:
  enum xxstatus { OK, RETRY, RPCERR, NOENT, IOERR };
  typedef int status;
  typedef unsigned long long lockid_t;
  typedef unsigned long long seqid_t;
  enum rpc_numbers {
    acquire = 0x7001,
    release = 0x7002,
    subscribe = 0x7003,	// for lab 5
    stat = 0x7004
  };
};
/*
#endif 

=======
=======
>>>>>>> 7d38a56bd943217b00fa2c747d980d8c2c5e1742
// lock protocol

#ifndef lock_protocol_h
#define lock_protocol_h

#include "rpc.h"

class lock_protocol {
 public:
  enum xxstatus { OK, RETRY, RPCERR, NOENT, IOERR };
  typedef int status;
  typedef unsigned long long lockid_t;
  enum rpc_numbers {
    acquire = 0x7001,
    release,
    subscribe,	// for lab 5
    stat
  };
};
<<<<<<< HEAD
*/
class rlock_protocol {
 public:
  typedef unsigned long long seqid_t;
  typedef unsigned long long lockid_t;
=======

class rlock_protocol {
 public:
>>>>>>> 7d38a56bd943217b00fa2c747d980d8c2c5e1742
  enum xxstatus { OK, RPCERR };
  typedef int status;
  enum rpc_numbers {
    revoke = 0x8001,
    retry = 0x8002
  };
};
#endif 
<<<<<<< HEAD
//>>>>>>> 05f0bfc6d075b8c62446b5d878ea5c8394953911
=======
>>>>>>> 7d38a56bd943217b00fa2c747d980d8c2c5e1742
