#pragma once

#include <stdbool.h>

// calls accept on listening socket fd and adds it to the epoll instance
bool accept_client(int proxy_fd, int epoll_fd);

bool handle_client(int client_fd);
