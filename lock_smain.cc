#include "rpc.h"
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include "lock_server_cache.h"
#include "paxos.h"
#include "rsm.h"
#include "jsl_log.h"

// Main loop of lock_server

int
main(int argc, char *argv[])
{
  int count = 0;

  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  srandom(getpid());

  if(argc != 3){
    fprintf(stderr, "Usage: %s [master:]port [me:]port\n", argv[0]);
    exit(1);
  }

  char *count_env = getenv("RPC_COUNT");
  if(count_env != NULL){
    count = atoi(count_env);
  }

  //jsl_set_debug(2);
  // Comment out the next line to switch between the ordinary lock
  // server and the RSM.  In Lab 7, we disable the lock server and
  // implement Paxos.  In Lab 8, we will make the lock server use your
  // RSM layer.
#define	RSM
#ifdef RSM
  rsm rsm(argv[1], argv[2]);
  lock_server_cache ls(&rsm);
  rsm.reg(lock_protocol::stat, &ls, &lock_server_cache::stat);
  rsm.reg(lock_protocol::acquire, &ls, &lock_server_cache::acquire);
  rsm.reg(lock_protocol::release, &ls, &lock_server_cache::release);
  rsm.reg(lock_protocol::subscribe, &ls, &lock_server_cache::subscribe);
#endif

#ifndef RSM
  lock_server_cache ls;
  rpcs server(atoi(argv[1]), count);
  server.reg(lock_protocol::stat, &ls, &lock_server_cache::stat);
  server.reg(lock_protocol::acquire, &ls, &lock_server_cache::acquire);
  server.reg(lock_protocol::release, &ls, &lock_server_cache::release);
  server.reg(lock_protocol::subscribe, &ls, &lock_server_cache::subscribe);
#endif


  while(1)
    sleep(1000);
}
