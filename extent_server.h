// this is the extent server

#ifndef extent_server_h
#define extent_server_h

#include <string>
#include <map>
#include <vector>
#include "extent_protocol.h"

class extent_server {

 public:
  extent_server();

  int put(extent_protocol::extentid_t id, std::string, int &);
  int get(extent_protocol::extentid_t id, std::string &);
  int getattr(extent_protocol::extentid_t id, extent_protocol::attr &a);
  int remove(extent_protocol::extentid_t id, int &);
  int readdir(extent_protocol::extentid_t dirid, std::map<std::string, extent_protocol::extentid_t>& entries);
  int createFile(extent_protocol::extentid_t parent, const char *name, mode_t mode, extent_protocol::extentid_t id);
  
  private:
	std::map<extent_protocol::extentid_t, std::string> fileid_content_m;
	std::map<extent_protocol::extentid_t, extent_protocol::attr> fileid_attr_m;
	std::map<extent_protocol::extentid_t, std::map<std::string, extent_protocol::extentid_t> > dirid_fmap_m;
	bool isfile(extent_protocol::extentid_t inum);
	bool isdir(extent_protocol::extentid_t inum);
	
};

#endif 







