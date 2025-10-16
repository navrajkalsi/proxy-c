#pragma once

#include <netdb.h>

#include "args.h"

bool setup_proxy(Config *config, int *proxy_fd);

// sets up epoll() and points out to the fd for new epoll instance
bool setup_async(int proxy_fd, int *epoll_fd);

// binds upstream to a fd and adds it to epoll instance
bool setup_upstream(const char *upstream);

bool start_proxy(int epoll_fd);
