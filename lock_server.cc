// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

lock_server::lock_server()
{
	pthread_mutex_init(&map_lock, NULL);
	pthread_cond_init(&lock_free, NULL);
}

lock_server::~lock_server()
{
	pthread_mutex_destroy(&map_lock);
	pthread_cond_destroy(&lock_free);
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&map_lock);
	r = 0;
	if (clt_lock_count.count(clt) != 0) {
		if (clt_lock_count[clt].count(lid) != 0) {
			r = clt_lock_count[clt][lid];
		}
	}
	pthread_mutex_unlock(&map_lock);
	return ret;
}

lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r) {
	lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&map_lock);
	// If the lid is in the map, the lock is hold by a client
	while (lock_client.count(lid) != 0) {
		pthread_cond_wait(&lock_free, &map_lock);
	}
	lock_client[lid] = clt;
	// Update acquire-count
	if (clt_lock_count.count(clt) != 0) {
		if (clt_lock_count[clt].count(lid) != 0) {
			clt_lock_count[clt][lid]++;
		} else {
			clt_lock_count[clt][lid] = 1;
			}
	} else {
		clt_lock_count[clt][lid] = 1;
	}
	pthread_mutex_unlock(&map_lock);
  
	return ret;
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r) {
  lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&map_lock);
	// If the lid is in the map, the lock is hold by a client
	if (lock_client.count(lid) != 0) {
		// if a client tries to release a lock it does not hold just return
		if (lock_client[lid] != clt) {
			pthread_mutex_unlock(&map_lock);
			return ret;
		}
	} else {
		pthread_mutex_unlock(&map_lock);
		return ret;
	}
	lock_client.erase(lid);
	pthread_mutex_unlock(&map_lock);
	pthread_cond_signal(&lock_free);
	return ret;
}