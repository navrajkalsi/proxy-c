#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "client.h"
#include "main.h"
#include "poll.h"
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

    Connection *conn = NULL;
    EventData *data = NULL;
    if (!(conn = init_connection()))
      return err("init_connection", NULL);
    conn->client_fd = client_fd;
    conn->client_events = EPOLLIN;

    if (!(data = init_event_data(TYPE_PTR_CLIENT, (epoll_data_t)(void *)conn)))
      return err("init_event_data", NULL);

    if (!add_to_epoll(epoll_fd, data, EPOLLIN))
      return err("add_to_epoll", strerror(errno));

    puts("accepted client");
  }

  return true;
}
