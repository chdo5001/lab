// extent wire protocol

#ifndef extent_protocol_h
#define extent_protocol_h

#include "rpc.h"
#include <stdio.h>

class extent_protocol {
 public:
  typedef int status;
  typedef unsigned long long extentid_t;
  enum xxstatus { OK, RPCERR, NOENT, IOERR, FBIG};
  enum rpc_numbers {
    put = 0x6001,
    get = 0x6002,
    getattr = 0x6003,
    remove = 0x6004,
	readdir = 0x6005,
	createFile = 0x6006,
	open = 0x6007,
	createDir = 0x6008,
	setMode = 0x6009,
	getMode = 0x6010,
	setAttr = 0x6011,
	write = 0x6012
  };
  static const unsigned int maxextent = 8192*1000;

  struct attr {
    unsigned int atime;
    unsigned int mtime;
    unsigned int ctime;
    unsigned int size;
  };
  
  struct dirent {
    std::string name;
    unsigned long long inum;
  };
};

inline unmarshall &
operator>>(unmarshall &u, extent_protocol::attr &a)
{
  u >> a.atime;
  u >> a.mtime;
  u >> a.ctime;
  u >> a.size;
  return u;
}

inline marshall &
operator<<(marshall &m, extent_protocol::attr a)
{
  m << a.atime;
  m << a.mtime;
  m << a.ctime;
  m << a.size;
  return m;
}
/*
inline marshall & 
operator<<(marshall &m, const long long l)
{
	printf("Marshall const long long");
	m << l;
	return m;
}

inline unmarshall &
operator>>(unmarshall &u, const long long &l)
{
	printf("Unmarshall const long long");
	u >> l;
	return u;
}
*/
#endif 
