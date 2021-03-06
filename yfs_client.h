#ifndef yfs_client_h
#define yfs_client_h

#include <string>
//#include "yfs_protocol.h"
#include <vector>
#include "extent_client.h"
#include "lock_protocol.h"
#include "lock_client_cache.h"

class lock_release_u : public lock_release_user {
  public:
	void dorelease(lock_protocol::lockid_t id);
	lock_release_u(extent_client* _ec);
  private:
	extent_client* ec;
};


class yfs_client {
  extent_client *ec;
  lock_client *lc;
  lock_release_u *lu;
  
 public:

  typedef unsigned long long inum;
  enum xxstatus { OK, RPCERR, NOENT, IOERR, FBIG };
  typedef int status;

  struct fileinfo {
    unsigned long long size;
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirinfo {
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirent {
    std::string name;
    unsigned long long inum;
  };

 private:
  static std::string filename(inum);
  static inum n2i(std::string);
 public:

  yfs_client(std::string, std::string);

  bool isfile(inum);
  bool isdir(inum);
  inum ilookup(inum id, const char* name);
  int readdir(inum id, std::list<dirent>& entries );
  int createFile(inum parent, const char *name, mode_t mode, inum& id);
  int setattr(inum id, fileinfo &finfo);
  int getfile(inum, fileinfo &);
  int getdir(inum, dirinfo &);
  int read(inum id,off_t off, size_t size, std::string &);
  int unlink(inum parent, const char *name);
  int open(inum id);
  int createDir(inum parent, const char* name, mode_t mode, inum& id);
  int write(inum id, off_t off, size_t size, const char* buf);
};

#endif 
