#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "connection.h"

// calls accept on listening socket fd and adds it to the epoll instance
void accept_client(int proxy_fd);

// called after EPOLLIN is detected on a client socketclient conn
void read_client(Connection *conn);

// whether to read more and how much to read more
bool verify_read(Connection *conn);

// Proxy side request verification
// host header verification
bool verify_request(Connection *conn);

// sending the error status code to client, in case of error during read()
void handle_error_response(Connection *conn);

bool generate_error_response(Connection *conn);

// to send an error directly without contacting upstream
bool write_error_response(Connection *conn);

bool write_str(const Connection *conn, const Str *write);
