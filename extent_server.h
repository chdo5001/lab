// this is the extent server

#ifndef extent_server_h
#define extent_server_h

#include <string>
#include <map>
#include <vector>
#include "extent_protocol.h"

class extent_server {

 public:
 
   typedef unsigned long long inum;
   
  extent_server();

  int put(extent_protocol::extentid_t id, std::string, int &);
  int write(extent_protocol::extentid_t id, unsigned long long off, size_t size, std::string buf, int&);
  int get(extent_protocol::extentid_t id, unsigned long long off, size_t size, std::string& buf);
  int getattr(extent_protocol::extentid_t id, extent_protocol::attr &a);
  int remove(extent_protocol::extentid_t pid, std::string name, int&);
  int readdir(extent_protocol::extentid_t dirid, std::map<std::string, extent_protocol::extentid_t>& entries);
  // TODO: Add the mode to list of arguments (and handle it...)
  int createFile(extent_protocol::extentid_t parent, std::string name, mode_t mode, extent_protocol::extentid_t& _id);
  int open(extent_protocol::extentid_t id, int&); 
  int createDir(extent_protocol::extentid_t parent, std::string name, int&);
  int setMode(extent_protocol::extentid_t id, mode_t mode, int&); 
  int getMode(extent_protocol::extentid_t id, mode_t& mode);
  int setAttr(extent_protocol::extentid_t id, extent_protocol::attr attr, int&);
  
  private:
	std::map<extent_protocol::extentid_t, int> fileid_open_m;
	std::map<extent_protocol::extentid_t, std::string> fileid_content_m;
	std::map<extent_protocol::extentid_t, std::string> fileid_name_m;
	std::map<extent_protocol::extentid_t, extent_protocol::extentid_t> fileid_dir_m;
	std::map<extent_protocol::extentid_t, extent_protocol::attr> fileid_attr_m;
	std::map<extent_protocol::extentid_t, std::map<std::string, extent_protocol::extentid_t> > dirid_fmap_m;
	std::map<extent_protocol::extentid_t, mode_t> fileid_mode_m;
	bool isfile(extent_protocol::extentid_t inum);
	bool isdir(extent_protocol::extentid_t inum);
	
};

#endif 
