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
  pthread_mutex_init(&map_lock, NULL);
  pthread_cond_init(&lock_free, NULL);
  sockaddr_in dstsock;
  make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() < 0) {
    printf("lock_client: call bind\n");
  }
}

lock_client::~lock_client() {
	pthread_mutex_destroy(&map_lock);
	pthread_cond_destroy(&lock_free);
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
 //  printf("Trying to acquire lock %016llx\n", lid);
  pthread_mutex_lock(&map_lock);
  while (lock_tid.count(lid) != 0) {
	pthread_cond_wait(&lock_free, &map_lock);
  }
  lock_tid[lid] = (unsigned int)pthread_self();
  pthread_mutex_unlock(&map_lock);
  int r;
  int ret = cl->call(lock_protocol::acquire, cl->id(), lid, r);
  assert (ret == lock_protocol::OK);
 // printf("Lock %016llx acquired\n", lid);
  return ret;
}

lock_protocol::status
lock_client::release(lock_protocol::lockid_t lid)
{
 //  printf("Trying to release lock %016llx\n", lid);
  int r = lock_protocol::OK;
  pthread_mutex_lock(&map_lock);
  if (lock_tid.count(lid) != 0) {
	if (lock_tid[lid] != (unsigned int)pthread_self()) {
		//printf("Tried to release foreign lock");
		return r;
	}
  }
  pthread_mutex_unlock(&map_lock);
  int ret = cl->call(lock_protocol::release, cl->id(), lid, r);
  assert (ret == lock_protocol::OK);
  pthread_mutex_lock(&map_lock);
  lock_tid.erase(lid);
  pthread_mutex_unlock(&map_lock);
  pthread_cond_signal(&lock_free);
 //  printf("Lock %016llx released\n", lid);
  return ret;
}

