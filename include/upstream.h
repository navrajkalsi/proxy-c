#pragma once

#include <stdbool.h>

#include "connection.h"

// fills upstream_addrinfo by calling getaddrinfo() on the upstream
// and selects the port matching the following, in order:
// if specified in the upstream with a ':'
// if an http protocol is specified int the beginning
// else fallbacks to FALLBACK_UPSTREAM_PORT
bool setup_upstream(char *upstream);

// prepares a new socket for upstream, connects to it points upstream_fd to the
// new socket fd
bool connect_upstream(int *upstream_fd);

void free_upstream_addrinfo(void);

// writing client request to upstream
void write_request(Connection *conn);

// reading response from upstream
void read_response(Connection *conn);
