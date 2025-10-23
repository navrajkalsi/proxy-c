#pragma once

#include <netdb.h>

#include "args.h"
#include "poll.h"

// global var that stores a linked list of struct addrinfo containing info about
// upstream server
// static struct addrinfo *upstream_addrinfo;

// global array of eventData structs that were added to the epoll table
// init_event_data() & free_event_data() add and remove from this array
// automatically
extern EventData *active_conns[MAX_CONNECTIONS];

bool setup_proxy(Config *config, int *proxy_fd);

// sets up epoll() and points out to the fd for new epoll instance
bool setup_epoll(int proxy_fd, int *epoll_fd);

// fills upstream_addrinfo by calling getaddrinfo() on the upstream
bool setup_upstream(const char *upstream);

// prepares a new socket for upstream, connects to it points upstream_fd to the
// new socket fd
bool connect_upstream(int *upstream_fd);

bool start_proxy(int epoll_fd);

// for atexit to free the global var
void free_upstream_addrinfo(void);

void free_active_conns(void);
