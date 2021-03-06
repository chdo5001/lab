// RPC stubs for clients to talk to lock_server

#include "lock_client.h"
#include "rpc.h"
#include <arpa/inet.h>

#include <sstream>
#include <iostream>
#include <stdio.h>
#include <sys/types.h>

lock_client::lock_client(std::string dst)
{
  //sockaddr_in dstsock;
  //make_sockaddr(dst.c_str(), &dstsock);
  //cl = new rpcc(dstsock);
  printf("lock_client::lock_client\n");
  cl = new rsm_client(dst);
  printf("lock_client::lock_client Done\n");
  //if (cl->bind() < 0) {
    //printf("lock_client: call bind\n");
  //}
}

lock_client::~lock_client() {

}

lock_protocol::status
lock_client::stat(lock_protocol::lockid_t lid)
{
  int r;
  int ret = cl->call(lock_protocol::stat, cl->id(), lid, r);
  assert (ret == lock_protocol::OK);
  return r;
}

lock_protocol::status
lock_client::acquire(lock_protocol::lockid_t lid)
{
  int r;
  int ret = cl->call(lock_protocol::acquire, cl->id(), (unsigned int) pthread_self(), lid, r);
  assert (ret == lock_protocol::OK);
  return ret;
}

lock_protocol::status
lock_client::release(lock_protocol::lockid_t lid)
{
  int r = lock_protocol::OK;
  int ret = cl->call(lock_protocol::release, cl->id(), (unsigned int) pthread_self(), lid, r);
  assert (ret == lock_protocol::OK);
  return ret;
}
