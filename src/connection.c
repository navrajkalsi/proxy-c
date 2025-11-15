#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "connection.h"
#include "main.h"
#include "proxy.h"
#include "utils.h"

Connection *init_conn(void) {
  Connection *conn;
  if (!(conn = malloc(sizeof(Connection)))) {
    err("malloc", strerror(errno));
    return NULL;
  }

  if (!activate_conn(conn)) {
    err("activate_conn",
        errno ? strerror(errno) : "Max limit of active connections reached");
    free(conn);
    return NULL;
  }

  memset(&conn->client_addr, 0, sizeof(struct sockaddr_storage));

  conn->state =
      READ_REQUEST; // remember to assign as ACCEPT_CLIENT, incase of proxy_fd

  Endpoint *client = &conn->client, *upstream = &conn->upstream;
  HTTP *client_http = &client->http, *upstream_http = &upstream->http;

  // same vars across client and upstream
  client->fd = upstream->fd = -1;
  client->buf_segment.len = upstream->buf_segment.len = 0;
  client->read_index = upstream->read_index = 0;
  client->to_read = upstream->to_read = BUFFER_SIZE - 1;
  client->to_write = upstream->to_write = 0;
  client->next_index = upstream->next_index = 0;
  client->chunked = upstream->chunked = false;
  client->headers_found = upstream->headers_found = false;
  *client->last_chunk_found = *upstream->last_chunk_found = '\0';
  client_http->version = upstream_http->version = STR(FALLBACK_HTTP_VER);
  client_http->connection = upstream_http->connection = ERR_STR;
  client_http->host = upstream_http->host = ERR_STR;
  client_http->path = upstream_http->path = ERR_STR;

  // client
  client->buf_segment.data =
      client->buffer; // initally request points to beginning of the buffer

  // upstream
  upstream->buf_segment.data = upstream->buffer;

  conn->proxy_fd = -1;
  conn->status = 0;

  return conn;
}

void free_conn(Connection **conn) {
  if (!conn || !*conn)
    return;

  Connection *to_free = *conn;
  deactivate_conn(*conn);

  free(to_free);
  to_free = NULL;
}

bool activate_conn(Connection *conn) {
  if (!conn)
    return set_efault();

  for (int i = 0; i < MAX_CONNECTIONS; ++i)
    if (!active_conns[i]) {
      active_conns[i] = conn;
      conn->self_ptr = active_conns + i;
      return true;
    }

  return false;
}

void deactivate_conn(Connection *conn) {
  if (!conn)
    return;

  *(conn->self_ptr) = NULL;
  conn->self_ptr = NULL;
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
bool add_to_epoll(Connection *conn, int fd, int flags) {
  // this struct does not need to be on the heap
  // kernel copies all the data into the epoll table
  struct epoll_event epoll_event = {.events = flags, .data.ptr = (void *)conn};

  if (fd == -1)
    return err("get_target_fd",
               "Socket is probably being added to epoll before "
               "accepting/connecting");

  if (epoll_ctl(EPOLL_FD, EPOLL_CTL_ADD, fd, &epoll_event) == -1)
    return err("epoll_ctl_add", strerror(errno));

  return true;
}

bool mod_in_epoll(Connection *conn, int fd, int flags) {
  struct epoll_event epoll_event = {.events = flags, .data.ptr = (void *)conn};

  if (fd == -1)
    return err("get_target_fd", "Socket fd is not initialized, logic error");

  if (epoll_ctl(EPOLL_FD, EPOLL_CTL_MOD, fd, &epoll_event) == -1)
    return err("epoll_ctl_mod", strerror(errno));

  return true;
}

bool del_from_epoll(int fd) {
  if (fd == -1)
    return err("get_target_fd", "Socket fd is not initialized");

  if (epoll_ctl(EPOLL_FD, EPOLL_CTL_DEL, fd, NULL) == -1)
    return err("epoll_ctl_del", strerror(errno));

  return true;
}
