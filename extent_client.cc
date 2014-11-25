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
}

extent_protocol::status
extent_client::read(extent_protocol::extentid_t id, off_t off, size_t size, std::string &buf)
{
	unsigned long long offset = (unsigned long long) off;
  extent_protocol::status ret = extent_protocol::OK;
  ret = cl->call(extent_protocol::get, id, offset, size, buf);
  return ret;
}

extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid, 
		       extent_protocol::attr &attr)
{
  extent_protocol::status ret = extent_protocol::OK;
  ret = cl->call(extent_protocol::getattr, eid, attr);
  return ret;
}

extent_protocol::status
extent_client::put(extent_protocol::extentid_t eid, std::string name)
{
  extent_protocol::status ret = extent_protocol::OK;
  //int r;
  ret = cl->call(extent_protocol::put, name);
  return ret;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t pid, const char* name)
{
  extent_protocol::status ret = extent_protocol::OK;
  int r;
  std::string str_name (name);
  printf("Ec str_name %s \n", str_name.c_str());
  ret = cl->call(extent_protocol::remove, pid, str_name, r);
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
extent_client::createFile(extent_protocol::extentid_t parent, const char *name) 
{
	extent_protocol::status ret = extent_protocol::OK;
	//extent_protocol::dirent dirent;
	std::string str_name (name);
	//dirent.inum = parent;
	int r;
	ret = cl->call(extent_protocol::createFile, parent, str_name, r);
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
	return cl->call(extent_protocol::setMode, id, mode, r);
}

extent_protocol::status 
extent_client::setAttr(extent_protocol::extentid_t id, extent_protocol::attr &attr) 
{
	//printf("extent_client: Call extent_server::setAttr  size: %d\n", attr.size);
	int r;
	int ret = cl->call(extent_protocol::setAttr, id, attr, r);
	return ret;
}

extent_protocol::status 
extent_client::write(extent_protocol::extentid_t id, off_t off, size_t size, const char* buf) {
	//printf("extent_client write enter\n");
	int r;
	unsigned long long offset = off;
/*
	printf("id: %016llx \n", id);
	printf("off: %d \n", off);
	printf("offset: %d \n", offset);
	printf("size: %d \n", size);
	printf("string: %s \n", buf);
	printf("Initialize string\n");*/
	std::string str_buf (buf);
	//printf("str_buf %s\n", str_buf.c_str());
	//printf("Call\n");
	r = cl->call(extent_protocol::write, id, offset, size, str_buf, r);
	//printf("fuse write exit\n");
	return r;
}