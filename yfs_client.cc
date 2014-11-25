// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include "lock_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);
  lc = new lock_client(lock_dst);

}

yfs_client::inum
yfs_client::n2i(std::string n)
{
  std::istringstream ist(n);
  unsigned long long finum;
  ist >> finum;
  return finum;
}

std::string
yfs_client::filename(inum inum)
{
  std::ostringstream ost;
  ost << inum;
  return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
  if(inum & 0x80000000)
    return true;
  return false;
}

bool
yfs_client::isdir(inum inum)
{
  return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
  int ret = lc->acquire(inum);
  if (ret != lock_protocol::OK) {
	goto release;
  }
  ret = ec->getattr(inum, a);
  if (ret != extent_protocol::OK) {
    goto release;
  }
//printf("getfile fileinfo atime %d\n", a.atime);
  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("getfile %016llx -> sz %llu\n", inum, fin.size);

 release:

	lc->release(inum);

  return ret;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int ret = lc->acquire(inum);
  if (ret != lock_protocol::OK) {
	goto release;
  }
  printf("getdir %016llx\n", inum);
  extent_protocol::attr a;
  ret = ec->getattr(inum, a);
  if (ret != extent_protocol::OK) {
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

 release:

	lc->release(inum);

  return ret;
}


int 
yfs_client::readdir(inum id, std::list<dirent>& entries ) {
	printf("readdir %016llx\n", id);
	dirent* entry = 0;
	std::map<std::string, extent_protocol::extentid_t>::iterator it;
	std::map<std::string, extent_protocol::extentid_t> entries_m;
	int ret = lc->acquire(id);
	if (ret != lock_protocol::OK) {
		goto release;
	}
	//std::list<dirent> dirent_l;
	// Change type of entries to map for rfc. Had problems with marshalling of lists
	//printf("Calling ec->readdir\n");
	ret = ec->readdir(id, entries_m);
	if (ret != extent_protocol::OK) {
		goto release;
	}
	//printf("Returned from readdir. entries.size() = %d\n", entries_m.size());
	//printf("Build up list\n");
	for (it = entries_m.begin(); it != entries_m.end(); it++) {
		entry = new dirent();
		entry->name = it->first;
		entry->inum = it->second;
		entries.push_back(*entry);
	}
  
  release:

		lc->release(id);

	//printf("Done\n");
	return OK;
}


// Does not need locking, as readdir() already has
yfs_client::inum 
yfs_client::ilookup(inum parent, const char* name) {
	//printf("Entering yfs_client::ilookup %s", name);
	//printf("in dir %016llx\n", parent);
	inum id = 0;
	dirent e;
	std::list<dirent> entries;
	readdir(parent, entries);
	//printf("Start comparing\n");
	std::string str_name (name);
	while(entries.size() != 0) {
		e = entries.front();
		//printf("Compare e.name %s ", e.name.c_str());
		//printf("with %s\n", str_name.c_str());
		if (e.name.compare(str_name) == 0) {
			//printf("Match. id is %016llx \", e.inum);
			id = e.inum;
			break;
		}
		entries.pop_front();
	}
	//printf("Exiting yfs_client:ilookup. Return id %d\n", id);
	return id;
}

int 
yfs_client::createFile(inum parent, const char *name, mode_t mode, inum& id) {
	//printf("Entering yfs_client createFile\n");
	extent_protocol::status ret = lc->acquire(parent);
	if (ret != lock_protocol::OK) {
		goto release;
	}
	ret = ec->createFile(parent, name);
	if (ret != extent_protocol::OK) {
		printf("ec createFile failed\n");
		lc->release(parent);
		goto release;
	}
	lc->release(parent);
	printf("Call lookup\n");
	id = ilookup(parent, name);
	// TODO: Should we rollback file creation if mode fails? (We should. But maybe we don't need to... :P)
	// PS: Looking into fuse::getattr(), I think we can ignore the mode completely
	ret = ec->setMode(id, mode);
	//printf("Exit yfs_client createfile. New fileid is %d\n", id);
  
  release:

	return ret;
}

int 
yfs_client::setattr(inum id, fileinfo& finfo){
     // std::string buf;
	extent_protocol::attr attr;
	attr.size = finfo.size;
	attr.mtime = 0;
	attr.ctime = 0;
	attr.atime = 0;
	extent_protocol::status ret = lc->acquire(id);
	if (ret != lock_protocol::OK) {
		goto release;
	}
	ret = ec->setAttr(id, attr);
	if (ret != lock_protocol::OK) {
		goto release;
	}
	//printf("ec::setAttr return value: %d\n", ret);
    /*if(ret != extent_protocol::OK){
        return IOERR;
    }*/
  release:

		lc->release(id);

    return ret;
}

int 
yfs_client::read(inum id, off_t off, size_t size, std::string& buf){
    extent_protocol::status ret = lc->acquire(id);
	if (ret != lock_protocol::OK) {
		goto release;
	}
    ret = ec->read(id, off, size, buf);
	if(ret != extent_protocol::OK){
        goto release;
    }
  release:

		lc->release(id);

    return ret;
}

int
yfs_client::unlink(inum parent, const char* name)
{
	extent_protocol::status ret = extent_protocol::IOERR;
	inum id = ilookup(parent, name);
	printf("Lookup returned: %016llx \n", id);
	if (id == 0) {
		goto release;
	}
    ret = lc->acquire(parent);
	if (ret != lock_protocol::OK) {
		goto release;
	}

	ret = lc->acquire(id);
	if (ret != lock_protocol::OK) {
		goto release;
	}
    ret = ec->remove(parent, name);
	if(ret != extent_protocol::OK){
        goto release;
    }
  release:
 
		lc->release(parent);


		lc->release(id);

    return ret;	
}

int 
yfs_client::open(inum id) 
{
	extent_protocol::status ret = lc->acquire(id);
	if (ret != lock_protocol::OK) {
		goto release;
	}	
	ret = ec->open(id);
	if(ret != extent_protocol::OK){
        goto release;
    }
	
  release:

		lc->release(id);

    return ret;	
}

int
yfs_client::createDir(inum parent, const char* name, mode_t mode, inum& id) 
{
	int ret = lc->acquire(parent);
	// TODO: The following check is actually never necessary, due to the assertion in the lock_client
	if (ret != lock_protocol::OK) {
		printf("yfs createDir no lock\n");
		goto release;
	}	
	ret = ec->createDir(parent, name);
	if (ret != extent_protocol::OK) {
		printf("yfs Create Dir failed\n");
		lc->release(parent);
		goto release;
	}
	// TODO: add rollback if setMode fails (see createFile)?
	// PS: Looking into fuse::getattr(), I think we can ignore the mode completely
//	ret = ec->setMode(id, mode);
	lc->release(parent);
	id = ilookup(parent, name);
  release:
	return ret; 
}

int 
yfs_client::write(inum id, off_t off, size_t size, const char* buf) 
{
	//printf("yfs_client write enter\n");
	int ret = lc->acquire(id);
	if (ret != lock_protocol::OK) {
		goto release;
	}	
	ret = ec->write(id, off, size, buf);
	if (ret != extent_protocol::OK) {
		goto release;
	}
	//printf("yfs_client write exit\n");
  release:

		lc->release(id);
	
	return ret; 
}
