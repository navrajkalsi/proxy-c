#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "args.h"
#include "poll.h"
#include "proxy.h"
#include "utils.h"

bool RUNNING = true;
Config config = {.port = NULL, .accept_all = false, .upstream = NULL};

int main(int argc, char *argv[]) {
  if (!setup_sig_handler()) {
    err("setup_sig_handler", strerror(errno));
    return -1;
  }

  config = parse_args(argc, argv);

  int proxy_fd = -1, epoll_fd = -1;

  if (!setup_proxy(&config, &proxy_fd)) {
    err("setup_proxy", strerror(errno));
    return -1;
  }

  // event data to be filled by setup_epoll
  // this is the pointer that will be added to the epoll instance for EPOLLIN
  EventData *proxy_event_data = NULL;

  if (!setup_epoll(proxy_fd, &epoll_fd, proxy_event_data)) {
    err("setup_async", strerror(errno));
    return -1;
  }

  // loading server info, into global var in proxy.c
  if (!setup_upstream(config.upstream)) {
    err("setup_upstream", strerror(errno));
    return -1;
  }

  if (!start_proxy(epoll_fd, proxy_event_data)) {
    err("start_proxy", strerror(errno));
    return -1;
  }

  return 0;
}
