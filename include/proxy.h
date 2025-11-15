#pragma once

#include <netdb.h>

#include "args.h"
#include "connection.h"
#include "main.h"
#include "poll.h"

// global array of conn structs that were added to the epoll table
// init & free conn() add and remove from this array
// automatically
extern Connection *active_conns[MAX_CONNECTIONS];

extern int EPOLL_FD;

bool setup_proxy(Config *config, int *proxy_fd);

// sets up epoll() and sets the global var EPOLL_FD for new epoll instance
bool setup_epoll(int proxy_fd);

bool start_proxy(void);

// mods state of the connection
void handle_state(Connection *conn);

void free_active_conns(void);

void log_state(int state);
