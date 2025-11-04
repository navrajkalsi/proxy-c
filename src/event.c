#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "event.h"
#include "main.h"
#include "proxy.h"
#include "utils.h"

Event *init_event(DataType data_type, epoll_data_t data) {
  Event *result;
  if (!(result = malloc(sizeof(Event)))) {
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

void free_event(Event **event) {
  if (!event || !*event)
    return;

  Event *to_free = *event;
  deactivate_event(*event);

  free(to_free);
  to_free = NULL;
};

Connection *init_connection(void) {
  Connection *conn;
  if (!(conn = malloc(sizeof(Connection)))) {
    err("malloc", strerror(errno));
    return NULL;
  }

  memset(&conn->client_addr, 0, sizeof(struct sockaddr_storage));

  conn->state = READ_REQUEST;

  // client
  conn->client_fd = -1;
  conn->client_headers.data =
      conn->client_buffer; // initally request points to beginning of the buffer
  conn->client_headers.len = 0;
  conn->read_index = 0;
  conn->to_read = BUFFER_SIZE - 1;
  conn->chunked = false;
  conn->headers_found = false;
  conn->next_index = 0;
  conn->client_status = 0;
  *conn->last_chunk = '\0';

  conn->client_fd = conn->upstream_fd = -1;
  conn->http_ver = STR(FALLBACK_HTTP_VER);
  conn->upstream_response = conn->request_host = conn->request_path =
      conn->connection = ERR_STR;
  return conn;
}

void free_connection(Connection **conn) {
  if (!conn || !*conn)
    return;

  free(*conn);
  *conn = NULL;
}

void free_event_conn(Event **event) {
  if (!event || !*event)
    return;

  Connection *conn = (*event)->data.ptr;
  if ((*event)->data_type != TYPE_FD)
    free_connection(&conn);

  free_event(event);
}

bool activate_event(Event *event) {
  if (!event)
    return set_efault();

  for (int i = 0; i < MAX_CONNECTIONS; ++i)
    if (!active_events[i]) {
      active_events[i] = event;
      event->self_ptr = active_events + i;
      return true;
    }

  return false;
}

void deactivate_event(Event *event) {
  if (!event)
    return;

  *(event->self_ptr) = NULL;
  event->self_ptr = NULL;
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
bool add_to_epoll(Event *event, int flags) {
  // this struct does not need to be on the heap
  // kernel copies all the data into the epoll table
  struct epoll_event epoll_event = {.events = flags, .data.ptr = (void *)event};
  int new_fd = -1;

  // only proxy_fd(listening sock) is added as fd
  if (event->data_type == TYPE_FD)
    new_fd = event->data.fd;
  else {
    Connection *conn = event->data.ptr;
    if (event->data_type == TYPE_PTR_CLIENT)
      new_fd = conn->client_fd;
    else if (event->data_type == TYPE_PTR_UPSTREAM)
      new_fd = conn->upstream_fd;
  }

  if (new_fd == -1)
    return err("get_target_fd",
               "Socket is probably being added to epoll before "
               "accepting/connecting, or check event.data_type");

  if (epoll_ctl(EPOLL_FD, EPOLL_CTL_ADD, new_fd, &epoll_event) == -1)
    return err("epoll_ctl", strerror(errno));

  return true;
}

bool mod_in_epoll(Event *event, int flags) {

  struct epoll_event epoll_event = {.events = flags, .data.ptr = (void *)event};
  int org_fd = -1;
  Connection *conn = event->data.ptr;

  if (event->data_type == TYPE_PTR_CLIENT)
    org_fd = conn->client_fd;
  else if (event->data_type == TYPE_PTR_UPSTREAM)
    org_fd = conn->upstream_fd;
  else
    return err("get_target_fd", "Data type is not valid for modification");

  if (org_fd == -1)
    return err("get_target_fd", "Socket fd is not initialized, logic error");

  if (epoll_ctl(EPOLL_FD, EPOLL_CTL_MOD, org_fd, &epoll_event) == -1)
    return err("epoll_ctl", strerror(errno));

  return true;
}

bool del_from_epoll(Event *event) {
  int org_fd = -1;
  Connection *conn = event->data.ptr;

  if (event->data_type == TYPE_PTR_CLIENT)
    org_fd = conn->client_fd;
  else if (event->data_type == TYPE_PTR_UPSTREAM)
    org_fd = conn->upstream_fd;
  else
    return err("get_target_fd", "Data type is not valid for modification");

  if (org_fd == -1)
    return err("get_target_fd", "Socket fd is not initialized, logic error");

  if (epoll_ctl(EPOLL_FD, EPOLL_CTL_DEL, org_fd, NULL) == -1)
    return err("epoll_ctl", strerror(errno));

  return true;
}
