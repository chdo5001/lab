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
	extent_protocol::attr* attr = new extent_protocol::attr();
	time_t raw_time ;
	time(&raw_time);
	//printf("rawtime %d\n", raw_time);
	attr->ctime = (unsigned int)raw_time;
	attr->mtime = (unsigned int)raw_time;
	attr->atime = (unsigned int)raw_time;
	fileid_attr_m[0x1] = *attr;
}

// TODO: review changes of ctime/atime/mtime

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
	//fileid_content_m[id] = buf;
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

int extent_server::get(extent_protocol::extentid_t id, unsigned long long off, size_t size, std::string& buf)
{	
	if (fileid_content_m.count(id) == 0) {
		return extent_protocol::NOENT;
	} 
	//printf("Reading from file %016llx %d bytes from offset: %d \n", id, size, off);
	buf = (fileid_content_m[id]).substr(off, size);
	//printf("Read from file: %s \n", buf.c_str());
	fileid_attr_m[id].atime = (unsigned int) time(NULL);
	return extent_protocol::OK;
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
	if (fileid_attr_m.count(id) == 0) {
		return extent_protocol::IOERR;
	}
	extent_protocol::attr attr = fileid_attr_m[id];
	a.size = attr.size;
	a.atime = attr.atime;
	a.mtime = attr.mtime;
	a.ctime = attr.ctime;
	return extent_protocol::OK;
}

int extent_server::remove(extent_protocol::extentid_t pid, std::string &name)
{
	if (dirid_fmap_m.count(pid) == 0) {
		// Directory containing file to remove not found
		return extent_protocol::NOENT;
	}
	if (dirid_fmap_m[pid].count(name) == 0) {
		// File/Directory to remove not found in directory pid
		return extent_protocol::NOENT;
	}
	extent_protocol::extentid_t id = dirid_fmap_m[pid][name];
	bool is_dir = isdir(id);
	if (is_dir) {
		if (dirid_fmap_m[id].size() != 0) {
			// Directory to delete not empty.
			return extent_protocol::IOERR;
		}
	}
	if (!is_dir) {
		fileid_content_m.erase(id);
	}
	fileid_attr_m.erase(id);
	dirid_fmap_m[fileid_dir_m[id]].erase(fileid_name_m[id]);
	fileid_name_m.erase(id);
	fileid_dir_m.erase(id);
	fileid_open_m.erase(id);
	// Not sure if the mode is correctly stored atm. Better check if there's an entry for id
	if (fileid_mode_m.count(id) != 0) {
		fileid_mode_m.erase(id);
	}
	
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
	// TODO: Make sure we have generated a unique number. If not, increase it until a unique one is found
	// Sth like while(fileid_attr_m.count(id) != 0) {id++}
	// Take care not to overwrite the first (file/dir bit)
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
	// TODO: Make sure we have generated a unique number. If not, increase it until a unique one is found
	// Sth like while(fileid_attr_m.count(id) != 0) {id++}
	// Take care not to overwrite the first (file/dir bit)
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
extent_server::setAttr(extent_protocol::extentid_t id, extent_protocol::attr attr, int&)
{
	//printf("extent_server::setAttr enter. Size: %d\n", attr.size);
	if (fileid_attr_m.count(id) == 0) {
		printf("Can't find entry %016llx in attribute list\n", id);
		return extent_protocol::NOENT;
	}
	//extent_protocol::attr* attr = &(fileid_attr_m[id]);
	//attr->size = size;
	//printf("Get file content\n");
	std::string content = fileid_content_m[id];
	//printf("Content: %s\n", content.c_str());
	//printf("Resize to %d\n", attr.size);
	content.resize(attr.size, '\0');
	//printf("New content: %s\n", content.c_str());
	int r;
	//printf("put\n");
	put(id, content, r);
	//printf("Return OK");
	return extent_protocol::OK;
}

int 
extent_server::write(extent_protocol::extentid_t id, unsigned long long off, size_t size, std::string buf, int&)
{
	//printf("extent_server write enter\n");
	/*
	if (size != buf.size()) {
		printf("str sizes do not match: %d  <->  %d", size, buf.size());
		exit(0);
	}*/
	if (fileid_content_m.count(id) == 0) {
		return extent_protocol::NOENT;
	} 
	std::string content = fileid_content_m[id];
	if (off + size > content.length()) {
		content.resize(off+size, '\0');
	}
	content.replace(off, size, buf, 0, size);
	//printf("Write content: %s\n", content.c_str());
	int r;
	put(id, content, r);
	//printf("extent_server write exit\n");
	return extent_protocol::OK;
}