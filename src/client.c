#include <assert.h>
#include <errno.h>
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
    Connection *conn = NULL;
    EventData *data = NULL;

    if (!(conn = init_connection()))
      return err("init_connection", NULL);

    if (!(data =
              init_event_data(TYPE_PTR_CLIENT, (epoll_data_t)(void *)conn))) {
      free_connection(&conn);
      return err("init_event_data", NULL);
    }
    socklen_t addr_len = sizeof conn->client_addr;

    if ((conn->client_fd =
             accept(proxy_fd, (struct sockaddr *)&conn->client_addr,
                    &addr_len)) == -1) {
      if (errno == EINTR && !RUNNING) // shutdown
        break;

      if (errno == EAGAIN || errno == EWOULDBLOCK) // no more connections
        break;

      if (errno == ECONNABORTED)
        // client aborted
        continue;

      free_event_conn(&data);
      return err("accept", strerror(errno));
    }

    if (!set_non_block(conn->client_fd)) {
      free_event_conn(&data);
      return err("set_non_block", strerror(errno));
    }

    if (!add_to_epoll(epoll_fd, data, EPOLLIN)) {
      free_event_conn(&data);
      return err("add_to_epoll", strerror(errno));
    }
  }

  return true;
}
