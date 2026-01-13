#include <openssl/err.h>

#include "url.h"

typedef struct connection Connection;

// IMPORTANT:
//
// canonical_host is used to validate the host header of requests, and provide a redirection url for
// 301 codes. The protocol used for redirection is determined by value of upstream_https. The port
// to use depends on the port value in canonical_host or upstream_https, in that order.
//
// upstream_host is used to connect() to the server. The port to use depends on the port value in
// upstream_host or upstream_https, in that order.
typedef struct config
{
  Host canonical_host, upstream_host;
  Str listen_port;
  bool accept_all, log_warnings, client_https, upstream_https;
} Config;

// sets up global ssl_context
bool setup_tls(void);

// sets up proxy using global config
bool setup_proxy(void);

// sets up epoll(), sets the global var EPOLL_FD for new epoll instance and adds PROXY_FD to epoll
bool setup_epoll(void);

bool start_proxy(void);

// mods state of the connection
void handle_state(Connection *conn);

void free_active_conns(void);

void free_config(void);

extern Config config;
extern SSL_CTX *ssl_context;
extern bool RUNNING;
extern int EPOLL_FD;
extern int PROXY_FD;
