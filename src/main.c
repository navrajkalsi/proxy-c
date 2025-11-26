#include "main.h"
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <regex.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "args.h"
#include "proxy.h"
#include "upstream.h"
#include "utils.h"

bool RUNNING = true;
Config config = {.port = NULL,
                 .canonical_host = NULL,
                 .accept_all = false,
                 .upstream = NULL,
                 .log_warnings = false};
int EPOLL_FD = -1;
regex_t origin_regex;

int main(int argc, char *argv[])
{
  if (!setup_sig_handler())
  {
    err("setup_sig_handler", strerror(errno));
    return -1;
  }

  if (!compile_regex())
    return -1;

  config = parse_args(argc, argv);

  int proxy_fd = -1;

  if (!setup_proxy(&config, &proxy_fd))
  {
    err("setup_proxy", NULL);
    return -1;
  }

  if (!setup_epoll(proxy_fd))
  {
    err("setup_epoll", NULL);
    return -1;
  }

  if (EPOLL_FD == -1)
  {
    err("verify_epoll_fd", "Epoll fd is not valid");
    return -1;
  }

  // loading server info, into global var in proxy.c
  if (!setup_upstream(config.upstream))
  {
    err("setup_upstream", NULL);
    return -1;
  }

  if (!start_proxy())
  {
    err("start_proxy", strerror(errno));
    return -1;
  }

  free_upstream_addrinfo();
  free_active_conns();
  free_config(&config);
  regfree(&origin_regex);

  return 0;
}
