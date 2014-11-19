#include "rpc.h"
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include "extent_server.h"

// Main loop of extent server

int
main(int argc, char *argv[])
{
  if(argc != 2){
    fprintf(stderr, "Usage: %s port\n", argv[0]);
    exit(1);
  }

  setvbuf(stdout, NULL, _IONBF, 0);

  rpcs server(atoi(argv[1]));
  extent_server ls;

  server.reg(extent_protocol::get, &ls, &extent_server::get);
  server.reg(extent_protocol::getattr, &ls, &extent_server::getattr);
  server.reg(extent_protocol::put, &ls, &extent_server::put);
  server.reg(extent_protocol::remove, &ls, &extent_server::remove);
  server.reg(extent_protocol::readdir, &ls, &extent_server::readdir);
  server.reg(extent_protocol::createFile, &ls, &extent_server::createFile);
  server.reg(extent_protocol::open, &ls, &extent_server::open);
  server.reg(extent_protocol::createDir, &ls, &extent_server::createDir);
  server.reg(extent_protocol::setMode, &ls, &extent_server::setMode);
  server.reg(extent_protocol::getMode, &ls, &extent_server::getMode);
  server.reg(extent_protocol::setAttr, &ls, &extent_server::setAttr);
  server.reg(extent_protocol::write, &ls, &extent_server::write);
  
  while(1)
    sleep(1000);
}
