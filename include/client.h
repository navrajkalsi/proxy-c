#pragma once

#include <stdbool.h>

#include "connection.h"

// calls accept on listening socket fd and adds it to the epoll instance
void accept_client(void);

// called after EPOLLIN is detected on a client socketclient conn
void read_request(Connection *conn);

// writing client request to upstream
void write_request(Connection *conn);
