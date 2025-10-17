#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "poll.h"
#include "utils.h"

EventData *init_event_data(DataType data_type, epoll_data_t data) {
  EventData *result;
  if (!(result = malloc(sizeof(EventData)))) {
    err("malloc", strerror(errno));
    return NULL;
  }

  result->data_type = data_type;
  result->data = data;
  return result;
}

void free_event_data(EventData **event_data) {
  if (!event_data || !*event_data)
    return;

  free(*event_data);
  *event_data = NULL;
};

Connection *init_connection(void) {
  Connection *result;
  if (!(result = malloc(sizeof(Connection)))) {
    err("malloc", strerror(errno));
    return NULL;
  }
  return result;
}

void free_connection(Connection **conn) {
  if (!conn || !*conn)
    return;

  free(*conn);
  *conn = NULL;
}

// after non_block all the system calls on this fd return instantly,
// like read() or write(). so we can deal with other fds and their events
// without waiting for this fd to finish
bool set_non_block(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    return enqueue_error("fcntl", strerror(errno));
  return true;
}

// adds the entry in the interest list of epoll instance
// essentially adds fd to epoll_fd list and the event specifies what to
// wait for & what fd to do that for
bool add_to_epoll(int epoll_fd, EventData *event_data, int flags) {
  // this struct does not need to be on the heap
  // kernel copies all the data into the epoll table
  struct epoll_event event = {.events = flags, .data.ptr = (void *)event_data};

  // only proxy_fd(listening sock) is added as fd
  if (event_data->data_type == TYPE_FD) {
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_data->data.fd, &event) == -1)
      return enqueue_error("epoll_ctl", strerror(errno));
  } else if (event_data->data_type == TYPE_PTR_CLIENT) { // add for client_fd
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD,
                  ((Connection *)event_data->data.ptr)->client_fd,
                  &event) == -1)
      return err("epoll_ctl", strerror(errno));
  } else if (event_data->data_type == TYPE_PTR_UPSTREAM) { // add for server_fd
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD,
                  ((Connection *)event_data->data.ptr)->server_fd, &event))
      return err("epoll_ctl", strerror(errno));
  }
  return true;
}
