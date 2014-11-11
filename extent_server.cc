// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
//#include <ctime>
#include <time.h>

extent_server::extent_server() {}

// TODO: review changes of ctime/atime/mtime

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

