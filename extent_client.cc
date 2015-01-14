// RPC stubs for clients to talk to extent_server

#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

// The calls assume that the caller holds a lock on the extent

extent_client::extent_client(std::string dst)
{
  sockaddr_in dstsock;
	make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() != 0) {
    printf("extent_client: bind failed\n");
  }
  pthread_mutexattr_init(&mutex_attr);
  pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&lock, &mutex_attr);
}

extent_client::~extent_client() {
	pthread_mutex_destroy(&lock);
	pthread_mutexattr_destroy(&mutex_attr);
}

extent_protocol::status
extent_client::read(extent_protocol::extentid_t id, off_t off, size_t size, std::string &buf)
{
	extent_protocol::status ret = extent_protocol::OK;
	pthread_mutex_lock(&lock);
	if (cache_status.count(id) == 0) {
		// File is not cached. Retrieve it
		std::string file_buf;
		extent_protocol::attr attr;
		mode_t mode;
		ret = getAttr(id, attr);
		assert (ret == extent_protocol::OK);
		ret = cl->call(extent_protocol::get, id, (unsigned long long) 0, attr.size, file_buf);
		assert (ret == extent_protocol::OK);
		// Attribute is changed by prior get() call. Update it.
		ret = getAttr(id, attr);
		assert (ret == extent_protocol::OK);
		ret = getMode(id, mode);
		assert (ret == extent_protocol::OK);
		cache_content[id] = file_buf;
		cache_attr[id] = attr;
		cache_mode[id] = mode;
		cache_status[id] = CACHED;
	} else {
		if (cache_status[id] == REMOVED) {
			pthread_mutex_unlock(&lock);
			return extent_protocol::NOENT;
		}
		(cache_attr[id]).atime = (unsigned int) time(NULL);
	}
	buf = (cache_content[id]).substr(off, size);
	//unsigned long long offset = (unsigned long long) off;
	//ret = cl->call(extent_protocol::get, id, offset, size, buf);	
	pthread_mutex_unlock(&lock);
	return ret;
}

extent_protocol::status
extent_client::getAttr(extent_protocol::extentid_t id, extent_protocol::attr &attr)
{
	extent_protocol::status ret = extent_protocol::OK;
	pthread_mutex_lock(&lock);
	if (cache_status.count(id) == 0) {
		ret = cl->call(extent_protocol::getattr, id, attr);	
	} else {
		attr = cache_attr[id];
	}
	pthread_mutex_unlock(&lock);
	return ret;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t id, const char* name)
{
	extent_protocol::status ret = extent_protocol::OK;
	int r;
	std::string str_name (name);
	pthread_mutex_lock(&lock);
	if (cache_status.count(id) != 0) {
		// Mark the cached file as deleted to prevent accesses to it
		// Just deleting it from the cache would result in an unnecessary rpc when trying to access it again
		cache_status[id] = REMOVED;
	}
	ret = cl->call(extent_protocol::remove, id, str_name, r);
	pthread_mutex_unlock(&lock);
	return ret;
}

extent_protocol::status
extent_client::readdir(extent_protocol::extentid_t di, std::map<std::string, extent_protocol::extentid_t>& entries)
{
//printf("extent_client readdir\n");
  extent_protocol::status ret = extent_protocol::OK;
  ret = cl->call(extent_protocol::readdir, di, entries);
  //printf("Return from extent_client readdir\n");
  return ret;
}

extent_protocol::status 
extent_client::createFile(extent_protocol::extentid_t parent, const char *name, mode_t mode) 
{
	extent_protocol::status ret = extent_protocol::OK;
	extent_protocol::extentid_t id;
	extent_protocol::attr attr;
	//extent_protocol::dirent dirent;
	std::string str_name (name);
	//dirent.inum = parent;
	ret = cl->call(extent_protocol::createFile, parent, str_name, mode, id);
	assert(ret == extent_protocol::OK);
	ret = getAttr(id, attr);
	assert(ret == extent_protocol::OK);
	pthread_mutex_lock(&lock);
	cache_content[id] = "";
	cache_attr[id] = attr;
	cache_mode[id] = mode;
	cache_status[id] = CACHED;
	pthread_mutex_unlock(&lock);
	return ret;
}

extent_protocol::status
extent_client::open(extent_protocol::extentid_t id) 
{
	extent_protocol::status ret = extent_protocol::OK;
	int r;
	ret = cl->call(extent_protocol::open, id, r);
	return ret;
}

extent_protocol::status
extent_client::createDir(extent_protocol::extentid_t parent, const char* name) 
{
	int r;
	std::string str_name (name);
	return cl->call(extent_protocol::createDir, parent, name, r);
}

extent_protocol::status
extent_client::setMode(extent_protocol::extentid_t id, mode_t mode) 
{
	int r;
	extent_protocol::status ret = extent_protocol::OK;
	pthread_mutex_lock(&lock);
	if (cache_status.count(id) == 0) {
		ret = cl->call(extent_protocol::setMode, id, mode, r);
	} else {
		cache_mode[id] = mode;
		(cache_attr[id]).ctime = (unsigned int) time(NULL);
		cache_status[id] = MODIFIED;
	}
	pthread_mutex_unlock(&lock);
	return ret;
}

extent_protocol::status
extent_client::getMode(extent_protocol::extentid_t id, mode_t& mode) 
{
	//int r;
	extent_protocol::status ret = extent_protocol::OK;
	pthread_mutex_lock(&lock);
	if (cache_status.count(id) == 0) {
		ret = cl->call(extent_protocol::getMode, id, mode);
	} else {
		cache_mode[id]= mode;
	}
	pthread_mutex_unlock(&lock);
	return ret;
}

extent_protocol::status 
extent_client::setAttr(extent_protocol::extentid_t id, extent_protocol::attr &attr) 
{
	//printf("extent_client: Call extent_server::setAttr  size: %d\n", attr.size);
	int r;
	extent_protocol::status ret = extent_protocol::OK;
	pthread_mutex_lock(&lock);
	if (cache_status.count(id) == 0) {
		ret = cl->call(extent_protocol::setAttr, id, attr, r);
	} else {
		cache_attr[id] = attr;
		cache_status[id] = MODIFIED;
	}
	pthread_mutex_unlock(&lock);
	return ret;
}

void 
extent_client::flush(extent_protocol::extentid_t id) 
{
	pthread_mutex_lock(&lock);
	if (cache_status.count(id) == 0) {
		pthread_mutex_unlock(&lock);
		return;
	} 
	extent_protocol::status ret = extent_protocol::OK;
	if (cache_status[id] == MODIFIED) {
		int r;
		ret = cl->call(extent_protocol::write, id, (unsigned long long) 0, cache_attr[id].size, cache_content[id], r);
		assert(ret == extent_protocol::OK);
		ret = cl->call(extent_protocol::setAttr, id, cache_attr[id], r);
		assert(ret == extent_protocol::OK);
		ret = cl->call(extent_protocol::setMode, id, cache_mode[id], r);
		assert(ret == extent_protocol::OK);
	} 
	cache_content.erase(id);
	cache_attr.erase(id);
	cache_mode.erase(id);
	cache_status.erase(id);
	pthread_mutex_unlock(&lock);
}

extent_protocol::status 
extent_client::write(extent_protocol::extentid_t id, off_t off, size_t size, const char* buf) {
	//printf("extent_client write enter\n");
	int r;
	extent_protocol::status ret = extent_protocol::OK;
	pthread_mutex_lock(&lock);
	std::string str_buf (buf, size);
	if (cache_status.count(id) == 0) {
		unsigned long long offset = off;
		
		ret = cl->call(extent_protocol::write, id, offset, (unsigned int) size, str_buf, r);
	} else {
		if (cache_status[id] == REMOVED) {
			pthread_mutex_unlock(&lock);
			return extent_protocol::NOENT;
		}
		std::string content = cache_content[id];
		if (off + size > content.length()) {
			content.resize(off+size, '\0');
		}
		content.replace(off, size, str_buf, 0, size);
		extent_protocol::attr attr;
		cache_content[id] = content;
		attr.size = content.size();
		attr.ctime = time(NULL);
		attr.mtime = attr.ctime;
		attr.atime = attr.ctime;
		cache_attr[id] = attr;
		cache_status[id] = MODIFIED;
	}
	pthread_mutex_unlock(&lock);
	return ret;
}

