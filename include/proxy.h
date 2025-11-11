#pragma once

#include <netdb.h>

#include "args.h"
#include "event.h"
#include "main.h"
#include "poll.h"

// global array of eventData structs that were added to the epoll table
// init_event_data() & free_event_data() add and remove from this array
// automatically
extern Event *active_events[MAX_CONNECTIONS];

extern int EPOLL_FD;

bool setup_proxy(Config *config, int *proxy_fd);

// sets up epoll() and sets the global var EPOLL_FD for new epoll instance
bool setup_epoll(int proxy_fd);

// fills upstream_addrinfo by calling getaddrinfo() on the upstream
// and selects the port matching the following, in order:
// if specified in the upstream with a ':'
// if an http protocol is specified int the beginning
// else fallbacks to FALLBACK_UPSTREAM_PORT
bool setup_upstream(char *upstream);

// prepares a new socket for upstream, connects to it points upstream_fd to the
// new socket fd
bool connect_upstream(int *upstream_fd);

bool start_proxy(void);

// mods state of the connection
void handle_state(Event *event_data);

void free_upstream_addrinfo(void);

void free_active_conns(void);
