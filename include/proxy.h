#pragma once

#include <netdb.h>

#include "args.h"
#include "poll.h"

// global var that stores a linked list of struct addrinfo containing info about
// upstream server
extern struct addrinfo *upstream_addrinfo;

bool setup_proxy(Config *config, int *proxy_fd);

// sets up epoll() and points out to the fd for new epoll instance
bool setup_epoll(int proxy_fd, int *epoll_fd, EventData *proxy_event_data);

// fills upstream_addrinfo by calling getaddrinfo() on the upstream
bool setup_upstream(const char *upstream);

// prepares a new socket for upstream, connects to it points upstream_fd to the
// new socket fd
bool connect_upstream(int *upstream_fd);

bool start_proxy(int epoll_fd, EventData *proxy_event_data);

// for atexit to free the global var
void free_upstream_addrinfo(void);
