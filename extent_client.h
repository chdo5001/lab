// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include <unistd.h>
#include "extent_protocol.h"
#include "rpc.h"

class extent_client {
	enum c_status { CACHED, MODIFIED, REMOVED };
  private:
	rpcc *cl;
	std::map<extent_protocol::extentid_t, std::string> cache_content;
	std::map<extent_protocol::extentid_t, extent_protocol::attr> cache_attr;
	std::map<extent_protocol::extentid_t, mode_t> cache_mode;
	std::map<extent_protocol::extentid_t, c_status> cache_status;
	pthread_mutex_t lock;
	pthread_mutexattr_t mutex_attr;
 
 public:
	extent_client(std::string dst);
	~extent_client();
  virtual extent_protocol::status read(extent_protocol::extentid_t id, off_t off, size_t size, std::string &buf);
  virtual extent_protocol::status getAttr(extent_protocol::extentid_t eid, extent_protocol::attr &a);
  virtual extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  virtual extent_protocol::status remove(extent_protocol::extentid_t pid, const char* name);
  virtual extent_protocol::status readdir(extent_protocol::extentid_t di, std::map<std::string, extent_protocol::extentid_t>& entries);
  virtual extent_protocol::status createFile(extent_protocol::extentid_t parent, const char* name);
  virtual extent_protocol::status open(extent_protocol::extentid_t id); 
  virtual extent_protocol::status createDir(extent_protocol::extentid_t parent, const char* name);
  virtual extent_protocol::status setMode(extent_protocol::extentid_t id, mode_t mode);
  virtual extent_protocol::status getMode(extent_protocol::extentid_t id, mode_t& mode);
  virtual extent_protocol::status setAttr(extent_protocol::extentid_t id, extent_protocol::attr &attr);
  virtual extent_protocol::status write(extent_protocol::extentid_t id, off_t off, size_t size, const char* buf);
};

#endif 
