#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "args.h"
#include "proxy.h"
#include "utils.h"

bool RUNNING = true;

int main(int argc, char *argv[]) {

  // atexits
  {
    atexit(free_error_list);
    atexit(print_error_list);
  }

  if (!setup_sig_handler()) {
    enqueue_error("setup_sig_handler", strerror(errno));
    return -1;
  }

  Config config = parse_args(argc, argv);

  int proxy_fd = -1, epoll_fd = -1;

  if (!setup_proxy(&config, &proxy_fd)) {
    enqueue_error("setup_proxy", strerror(errno));
    return -1;
  }

  if (!setup_async(proxy_fd, &epoll_fd)) {
    enqueue_error("setup_async", strerror(errno));
    return -1;
  }

  // loading server info
  if (!setup_upstream(config.upstream){
    enqueue_error("setup_upstream", strerror(errno));
    return -1;
  }

  if (!start_proxy(epoll_fd)) {
    enqueue_error("start_proxy", strerror(errno));
    return -1;
  }

  return 0;
}
