#pragma once

#include "args.h"

bool setup_proxy(Config *config, int *proxy_fd);

// sets up epoll() and points out to the fd for new epoll instance
bool setup_async(int proxy_fd, int *epoll_fd);

bool start_proxy(int epoll_fd);
