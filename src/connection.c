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

  // client
  conn->client_fd = -1;
  conn->client_headers.data =
      conn->client_buffer; // initally request points to beginning of the buffer
  conn->client_headers.len = 0;
  conn->read_index = 0;
  conn->write_index = 0;
  conn->to_read = BUFFER_SIZE - 1;
  conn->to_write = 0;
  conn->chunked = false;
  conn->next_index = 0;
  conn->headers_found = false;
  *conn->last_chunk_found = '\0';
  conn->client_status = 0;
  conn->http_ver = STR(FALLBACK_HTTP_VER);
  conn->connection = ERR_STR;
  conn->host = ERR_STR;
  conn->path = ERR_STR;

  // upstream
  conn->upstream_fd = -1;
  conn->upstream_response = ERR_STR;

  // proxy
  conn->proxy_fd = -1;

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
