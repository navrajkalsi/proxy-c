#pragma once

#include "poll.h"
#include <stdbool.h>

// calls accept on listening socket fd and adds it to the epoll instance
bool accept_client(int proxy_fd, int epoll_fd);

// called after EPOLLIN is detected on a client socket
bool read_client(EventData *event_data);
