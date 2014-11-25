// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include "extent_protocol.h"
#include "rpc.h"

class extent_client {
 private:
  rpcc *cl;

 public:
  extent_client(std::string dst);

  extent_protocol::status read(extent_protocol::extentid_t id, off_t off, size_t size, std::string &buf);
  extent_protocol::status getattr(extent_protocol::extentid_t eid, extent_protocol::attr &a);
  extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  extent_protocol::status remove(extent_protocol::extentid_t pid, const char* name);
  extent_protocol::status readdir(extent_protocol::extentid_t di, std::map<std::string, extent_protocol::extentid_t>& entries);
  extent_protocol::status createFile(extent_protocol::extentid_t parent, const char* name);
  extent_protocol::status open(extent_protocol::extentid_t id); 
  extent_protocol::status createDir(extent_protocol::extentid_t parent, const char* name);
  extent_protocol::status setMode(extent_protocol::extentid_t id, mode_t mode);
  extent_protocol::status setAttr(extent_protocol::extentid_t id, extent_protocol::attr &attr);
  extent_protocol::status write(extent_protocol::extentid_t id, off_t off, size_t size, const char* buf);
};

#endif 

