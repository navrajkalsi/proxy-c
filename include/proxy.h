#include "url.h"     //
#include <stdbool.h> //

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

extern Config config;
extern bool RUNNING;
