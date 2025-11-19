#pragma once

#include <stdbool.h>

#include "connection.h"

// calls accept on listening socket fd and adds it to the epoll instance
void accept_client(int proxy_fd);

// called after EPOLLIN is detected on a client socketclient conn
void read_request(Connection *conn);

// whether to read more and how much to read more
bool verify_read(Connection *conn);

// Proxy side request verification
// host header verification
bool verify_request(Connection *conn);

// writing client request to upstream
void write_request(Connection *conn);
