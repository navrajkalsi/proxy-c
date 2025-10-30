#pragma once

#include <stdbool.h>

#include "event.h"

// calls accept on listening socket fd and adds it to the epoll instance
bool accept_client(int proxy_fd, int epoll_fd);

// called after EPOLLIN is detected on a client socketclient conn
bool read_client(const Event *event);

// whether to read more and how much to read more
bool verify_read(Connection *conn);

bool handle_response_client(const Event *event);

// to send an error directly without contacting upstream
bool write_error_response(Connection *conn);

bool write_str(const Connection *conn, const Str *write);
