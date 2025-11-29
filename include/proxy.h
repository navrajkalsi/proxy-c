#pragma once

#include <openssl/crypto.h>

#include "connection.h"

extern int EPOLL_FD;

SSL_CTX *setup_tls(void);

bool setup_proxy(const Config *config, int *proxy_fd);

// sets up epoll() and sets the global var EPOLL_FD for new epoll instance
bool setup_epoll(int proxy_fd);

bool start_proxy(void);

// mods state of the connection
void handle_state(Connection *conn);

void free_active_conns(void);
