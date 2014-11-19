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
//printf("Enter extent_server put");
	extent_protocol::attr* attr = new extent_protocol::attr();
	time_t raw_time ;
	time(&raw_time);
	fileid_content_m[id] = buf;
	attr->size = buf.size();
	//printf("rawtime %d\n", raw_time);
	attr->ctime = (unsigned int)raw_time;
	attr->mtime = (unsigned int)raw_time;
	attr->atime = (unsigned int)raw_time;
	fileid_attr_m[id] = *attr;
	fileid_content_m[id] = buf;
	//printf("Exit put\n");
	return extent_protocol::OK;
}

int extent_server::readdir(extent_protocol::extentid_t dirid, std::map<std::string, extent_protocol::extentid_t>& entries) {
//printf("Extent_server readdir start\n");
	if (dirid_fmap_m.count(dirid) == 0) {
		return extent_protocol::IOERR;
	}
	//extent_protocol::dirent* entry;
	std::map<std::string, extent_protocol::extentid_t>* fmap = &dirid_fmap_m[dirid];
	std::map<std::string, extent_protocol::extentid_t>::iterator fmap_it = fmap->begin();
	for (; fmap_it != fmap->end(); fmap_it++) {
		entries[fmap_it->first] = fmap_it->second;
	}
	//printf("Extent_server readdir exit\n");
	return extent_protocol::OK;
}

int extent_server::get(extent_protocol::extentid_t id, off_t off, size_t size, std::string& buf)
{	
	if (fileid_content_m.count(id) == 0) {
		return extent_protocol::NOENT;
	} 
	//printf("Reading from file %016llx %d bytes f\n", id, size, off);
	buf = (fileid_content_m[id]).substr(off, size);
	printf("%s \n", buf.c_str());
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
	if (fileid_name_m.count(id) == 0) {
		return extent_protocol::IOERR;
	}
	fileid_content_m.erase(id);
	fileid_attr_m.erase(id);
	dirid_fmap_m[fileid_dir_m[id]].erase(fileid_name_m[id]);
	fileid_name_m.erase(id);
	fileid_dir_m.erase(id);
	fileid_open_m.erase(id);
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

int extent_server::createFile(extent_protocol::extentid_t parent, std::string name, int&)
{
	//extent_protocol::extentid_t parent = dirent.inum;
	//std::string name = dirent.name;
	// TODO: Add error handling (check for duplicate names, etc)
	// TODO: store the mode somewhere
    
        // TODO: does creating a dir also work (fuse::mkdir)? 
	//printf("Create new file: %s", name.c_str());
	//printf("in dir: %d\n", parent);
	int fd;
	uint64_t num =0; 
	if ((fd = ::open("/dev/random", O_RDONLY)) == -1)
	{
		exit(2);
	}
	while (num < 2) {
		read(fd, &num, 8);
	}
	close(fd);
	// We're only interested in the last 32bit, as Fuse only uses 32bit-ids. Set the first 32bit to 0
	extent_protocol::extentid_t id = num & 0xFFFFFFFF;
	// and the first bit of the remaining 32 to 1 (for file)
	id = id | 0x80000000;
	int r;
	//printf("New id = %d\n", id); 
	// TODO: Move put() behind the condition
	put(id, "", r);
	if (dirid_fmap_m.count(parent) == 0) {
		return extent_protocol::NOENT;
	}
	//std::string* str = new std::string(name);
	dirid_fmap_m[parent][name] = id;
	fileid_dir_m[id] = parent;
	fileid_name_m[id] = name;
	fileid_open_m[id] = 0;
	//printf("dirid_fmap[parent].size(): %d\n",(dirid_fmap_m[parent]).size()); 
	//return extent_protocol::NOENT;
	return extent_protocol::OK;
}

int 
extent_server::open(extent_protocol::extentid_t id, int&) 
{
	time_t raw_time ;
	time(&raw_time);
	// TODO: is there something else to do here in this method?
	fileid_open_m[id] = fileid_open_m[id] + 1;
	extent_protocol::attr* attr = &(fileid_attr_m[id]);
	attr->atime = (unsigned int) raw_time;
	return extent_protocol::OK;
}

int 
extent_server::createDir(extent_protocol::extentid_t parent, std::string name, int&)
{
	int fd;
	uint64_t num =0; 
	if ((fd = ::open("/dev/random", O_RDONLY)) == -1)
	{
		exit(2);
	}
	while (num < 2) {
		read(fd, &num, 8);
	}
	close(fd);
	// We're only interested in the last 32bit, as Fuse only uses 32bit-ids. Set the first 32bit to 0
	// and the first bit of the remaining 32 to 0 (for dir)
	inum id = num & 0x7FFFFFF;
	
	extent_protocol::attr* attr = new extent_protocol::attr();
	time_t raw_time ;
	time(&raw_time);
	//fileid_content_m[id] = buf;
	//attr->size = buf.size();
	//printf("rawtime %d\n", raw_time);
	attr->ctime = (unsigned int)raw_time;
	attr->mtime = (unsigned int)raw_time;
	attr->atime = (unsigned int)raw_time;

	//int r;
	//printf("New id = %d\n", id); 
	//put(id, "", r);
	if (dirid_fmap_m.count(parent) == 0) {
		return extent_protocol::NOENT;
	}
	//std::string* str = new std::string(name);
	fileid_attr_m[id] = *attr;
	dirid_fmap_m[parent][name] = id;
	fileid_dir_m[id] = parent;
	fileid_name_m[id] = name;
	//fileid_open_m[id] = 0;
	//printf("dirid_fmap[parent].size(): %d\n",(dirid_fmap_m[parent]).size()); 
	//return extent_protocol::NOENT;
	return extent_protocol::OK;
}

int
extent_server::setMode(extent_protocol::extentid_t id, mode_t mode, int&) 
{ 
	fileid_mode_m[id] = mode;
	return extent_protocol::OK;
}

int 
extent_server::getMode(extent_protocol::extentid_t id, mode_t& mode) 
{ 
	if (fileid_mode_m.count(id) == 0) {
		return extent_protocol::NOENT;
	}
	mode = fileid_mode_m[id];
	return extent_protocol::OK; 
}

int 
extent_server::setAttr(extent_protocol::extentid_t id, unsigned long long size, int&)
{
	if (fileid_attr_m.count(id) == 0) {
		return extent_protocol::NOENT;
	}
	//extent_protocol::attr* attr = &(fileid_attr_m[id]);
	//attr->size = size;
	std::string content = fileid_content_m[id];
	content.resize(size, '\0');
	int r;
	put(id, content, r);
	return extent_protocol::OK;
}

int 
extent_server::write(extent_protocol::extentid_t id, off_t off, size_t size, std::string buf, int&)
{
	if (size != buf.size()) {
		printf("str sizes do not match");
		exit(0);
	}
	if (fileid_content_m.count(id) == 0) {
		return extent_protocol::NOENT;
	} 
	std::string content = fileid_content_m[id];
	if (off + size > content.length()) {
		content.resize(off+size, '\0');
	}
	content.replace(off, size, buf);
	int r;
	put(id, content, r);
	return extent_protocol::OK;
}