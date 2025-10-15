#include <asm-generic/errno.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "client.h"
#include "main.h"
#include "proxy.h"
#include "utils.h"

bool accept_client(int proxy_fd, int epoll_fd) {
  // looping, as there epoll might be waken up by multiple incoming requests on
  // this fd
  while (RUNNING) {
    int client_fd = -1;
    if ((client_fd = accept(proxy_fd, NULL, NULL)) == -1) {
      if (errno == EINTR && !RUNNING) // shutdown
        break;

      if (errno == EAGAIN || errno == EWOULDBLOCK)
        // no more connections
        break;

      if (errno == ECONNABORTED)
        // client aborted
        continue;

      return err("accept", strerror(errno));
    }

    if (!set_non_block(client_fd))
      return err("set_non_block", strerror(errno));

    if (!add_to_epoll(epoll_fd, client_fd, EPOLLIN))
      return err("add_to_epoll", strerror(errno));

    puts("accepted client");
  }

  return true;
}
