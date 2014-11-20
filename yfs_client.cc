// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
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
  int r = OK;
  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
//printf("getfile fileinfo atime %d\n", a.atime);
  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("getfile %016llx -> sz %llu\n", inum, fin.size);

 release:

  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;
  printf("getdir %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

 release:
  return r;
}


int 
yfs_client::readdir(inum di, std::list<dirent>& entries ) {
	//printf("readdir %016llx\n", di);
	//std::list<dirent> dirent_l;
	// Change type of entries to map for rfc. Had problems with marshalling of lists
	std::map<std::string, extent_protocol::extentid_t> entries_m;
	//printf("Calling ec->readdir\n");
	if (ec->readdir(di, entries_m) != extent_protocol::OK) {
		return IOERR;
	}
	//printf("Returned from readdir. entries.size() = %d\n", entries_m.size());
	dirent* entry = 0;
	std::map<std::string, extent_protocol::extentid_t>::iterator it;
	//printf("Build up list\n");
	for (it = entries_m.begin(); it != entries_m.end(); it++) {
		entry = new dirent();
		entry->name = it->first;
		entry->inum = it->second;
		entries.push_back(*entry);
	}
	//printf("Done\n");
	return OK;
}


yfs_client::inum 
yfs_client::ilookup(inum di, const char* name) {
	//printf("Entering yfs_client::ilookup %s", name);
	//printf("in dir %d\n", di);
	inum id = 0;
	dirent e;
	std::list<dirent> entries;
	readdir(di, entries);
	//printf("Start comparing\n");
	std::string str_name (name);
	while(entries.size() != 0) {
		e = entries.front();
		//printf("Compare e.name %s ", e.name.c_str());
		//printf("with %s\n", str_name.c_str());
		if (e.name.compare(str_name) == 0) {
			//printf("Match. id is %d", e.inum);
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
	extent_protocol::status ret = ec->createFile(parent, name);
	if (ret != extent_protocol::OK) {
		return ret;
	}
	//printf("Call lookup\n");
	id = ilookup(parent, name);
	// TODO: Should we rollback file creation if mode fails? (We should. But maybe we don't need to... :P)
	// PS: Looking into fuse::getattr(), I think we can ignore the mode completely
	ret = ec->setMode(id, mode);
	//printf("Exit yfs_client createfile. New fileid is %d\n", id);
	return ret;
}

int 
yfs_client::setattr(inum id, fileinfo& finfo){
    extent_protocol::status ret;
    // std::string buf;
	extent_protocol::attr attr;
	attr.size = finfo.size;
	attr.mtime = 0;
	attr.ctime = 0;
	attr.atime = 0;
    ret = ec->setAttr(id, attr);
	//printf("ec::setAttr return value: %d\n", ret);
    /*if(ret != extent_protocol::OK){
        return IOERR;
    }*/
    return ret;
}

int 
yfs_client::read(inum di, off_t off, size_t size, std::string& buf){
    extent_protocol::status ret;
    ret = ec->read(di, off, size, buf);
	if(ret != extent_protocol::OK){
        return IOERR;
    }
    return ret;
}

int 
yfs_client::unlink(inum di, const char* name){
    //TODO: implementieren ->  Remove a file name of the dir di
	return IOERR;
}

int 
yfs_client::open(inum id) {
	extent_protocol::status ret;
	ret = ec->open(id);
	return ret;
}

int
yfs_client::createDir(inum parent, const char* name, mode_t mode, inum& id) 
{
	int r = ec->createDir(parent, name);
	if ( r != extent_protocol::OK) {
		return r;
	}
	id = ilookup(parent, name);
	// TODO: add rollback if setMode fails (see createFile)?
	// PS: Looking into fuse::getattr(), I think we can ignore the mode completely
	r = ec->setMode(id, mode);
	return r; 
}

int 
yfs_client::write(inum id, off_t off, size_t size, const char* buf) 
{
	//printf("yfs_client write enter\n");
	int r = ec->write(id, off, size, buf);
	//printf("yfs_client write exit\n");
	return r;
}