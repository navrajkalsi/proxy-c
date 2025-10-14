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

int main(int argc, char *argv[]) {

  // atexits
  {
    atexit(free_error_list);
    atexit(print_error_list);
  }

  Config config = parse_args(argc, argv);

  int proxy_fd = -1, epoll_fd = -1;

  if (!setup_proxy(&config, &proxy_fd))
    enqueue_error("setup_proxy", strerror(errno));

  if (!setup_async(proxy_fd, &epoll_fd))
    enqueue_error("setup_async", strerror(errno));

  if (!start_proxy(proxy_fd))
    enqueue_error("start_proxy", strerror(errno));

  return 0;
}
