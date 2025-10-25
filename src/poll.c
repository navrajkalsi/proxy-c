#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "poll.h"
#include "proxy.h"
#include "utils.h"

EventData *init_event_data(DataType data_type, epoll_data_t data) {
  EventData *result;
  if (!(result = malloc(sizeof(EventData)))) {
    err("malloc", strerror(errno));
    return NULL;
  }

  result->data_type = data_type;
  result->data = data;

  if (!activate_event(result)) {
    err("activate_event",
        errno ? strerror(errno) : "Max limit of active connections reached");
    free(result);
    return NULL;
  }

  return result;
}

void free_event_data(EventData **event_data) {
  if (!event_data || !*event_data)
    return;

  EventData *to_free = *event_data;
  deactivate_event(*event_data);

  free(to_free);
  to_free = NULL;
};

Connection *init_connection(void) {
  Connection *result;
  if (!(result = malloc(sizeof(Connection)))) {
    err("malloc", strerror(errno));
    return NULL;
  }

  result->client_fd = result->upstream_fd = -1;
  result->client_status = 0;
  result->http_ver = STR(FALLBACK_HTTP_VER);
  result->client_request = result->upstream_response = result->request_host =
      result->request_path = result->connection = ERR_STR;
  memset(&result->client_addr, 0, sizeof(struct sockaddr_storage));
  return result;
}

void free_connection(Connection **conn) {
  if (!conn || !*conn)
    return;

  free(*conn);
  *conn = NULL;
}

void free_event_conn(EventData **event_data) {
  if (!event_data || !*event_data)
    return;

  Connection *conn = (*event_data)->data.ptr;
  if ((*event_data)->data_type != TYPE_FD)
    free_connection(&conn);

  free_event_data(event_data);
}

bool activate_event(EventData *event_data) {
  if (!event_data)
    return set_efault();

  for (int i = 0; i < MAX_CONNECTIONS; ++i)
    if (!active_conns[i]) {
      active_conns[i] = event_data;
      event_data->self_ptr = active_conns + i;
      return true;
    }

  return false;
}

void deactivate_event(EventData *event_data) {
  if (!event_data)
    return;

  *(event_data->self_ptr) = NULL;
  event_data->self_ptr = NULL;
}

// after non_block all the system calls on this fd return instantly,
// like read() or write(). so we can deal with other fds and their
// events without waiting for this fd to finish
bool set_non_block(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    return err("fcntl", strerror(errno));
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
      return err("epoll_ctl", strerror(errno));
  } else if (event_data->data_type == TYPE_PTR_CLIENT) { // add for client_fd
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD,
                  ((Connection *)event_data->data.ptr)->client_fd,
                  &event) == -1)
      return err("epoll_ctl", strerror(errno));
  } else if (event_data->data_type == TYPE_PTR_UPSTREAM) { // add for server_fd
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD,
                  ((Connection *)event_data->data.ptr)->upstream_fd, &event))
      return err("epoll_ctl", strerror(errno));
  }
  return true;
}
