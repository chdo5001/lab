// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <inttypes.h>  

extent_server::extent_server() 
{
	std::map<std::string, extent_protocol::extentid_t> dir_entries;
	dirid_fmap_m[0x1] = dir_entries;
}

// TODO: review changes of ctime/atime/mtime
// TODO: extend dirid_fmap_m to store not only files, but dirs too (actually, this only needs additional isFile-checks 
// 		 in some methods (e.g. readdir)

int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
	extent_protocol::attr* attr = new extent_protocol::attr();
	time_t raw_time ;
	time(&raw_time);
	fileid_content_m[id] = buf;
	attr->size = buf.size();
	attr->ctime = (unsigned int)raw_time;
	attr->mtime = (unsigned int)raw_time;
	attr->atime = (unsigned int)raw_time;
	fileid_attr_m[id] = *attr;
	return extent_protocol::OK;
}

int extent_server::readdir(extent_protocol::extentid_t dirid, std::map<std::string, extent_protocol::extentid_t>& entries) {
	if (dirid_fmap_m.count(dirid) == 0) {
		return extent_protocol::IOERR;
	}
	//extent_protocol::dirent* entry;
	std::map<std::string, extent_protocol::extentid_t>* fmap = &dirid_fmap_m[dirid];
	std::map<std::string, extent_protocol::extentid_t>::iterator fmap_it = fmap->begin();
	for (; fmap_it != fmap->end(); fmap_it++) {
		entries[fmap_it->first] = fmap_it->second;
	}
	
	return extent_protocol::OK;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{	

		if (fileid_content_m.count(id) == 0) {
			return extent_protocol::IOERR;
		} 
	
		buf = fileid_content_m[id];
		fileid_attr_m[id].atime = (unsigned int) time(NULL);
	
	return extent_protocol::OK;
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
	if (fileid_content_m.count(id) == 0) {
		return extent_protocol::IOERR;
	}
	extent_protocol::attr attr = fileid_attr_m[id];
	a.size = attr.size;
	a.atime = attr.atime;
	a.mtime = attr.mtime;
	a.ctime = attr.ctime;
	return extent_protocol::OK;
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
	if (fileid_content_m.count(id) == 0) {
		return extent_protocol::IOERR;
	}
	fileid_content_m.erase(id);
	fileid_attr_m.erase(id);
	return extent_protocol::OK;
}
bool extent_server::isfile(extent_protocol::extentid_t id)
{
  if(id & 0x80000000)
    return true;
  return false;
}

bool extent_server::isdir(extent_protocol::extentid_t id)
{
  return ! isfile(id);
}

int extent_server::createFile(extent_protocol::extentid_t parent, const char *name, mode_t mode, extent_protocol::extentid_t id)
{

	// TODO: Add error handling (check for duplicate names, etc)
	// TODO: store the mode somewhere
	// TODO: check correctness of random number generation

	int fd;
	uint64_t num; 
	if ((fd = open("/dev/random", O_RDONLY)) == -1)
	{
		exit(2);
	}
	read(fd, &num, 8);
	close(fd);
	
	id = num;
	int r;
	put(id, "", r);
	if (dirid_fmap_m.count(parent) == 0) {
		return extent_protocol::NOENT;
	}
	std::string* str = new std::string(name);
	dirid_fmap_m[parent][*str] = id;
	//return extent_protocol::NOENT;
	return extent_protocol::OK;
}

