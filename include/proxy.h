#pragma once

#include "args.h"

bool setup_proxy(Config *config, int *proxy_fd);

// sets up epoll() and points out to the fd for new epoll instance
bool setup_async(int proxy_fd, int *epoll_fd);

bool start_proxy(int proxy_fd, int epoll_fd);

// calls fcntl to set non block option on a socket
bool set_non_block(int sock_fd);

// calls epoll_ctl to add
bool add_to_epoll(int epoll_fd, int new_fd, int flags);
