#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "connection.h"
#include "main.h"
#include "upstream.h"
#include "utils.h"

// global var that stores a linked list of struct addrinfo containing info about
// upstream server
struct addrinfo *upstream_addrinfo = NULL;

bool setup_upstream(char *upstream) {
  if (!upstream)
    return set_efault();

  // removing / at the end, if found
  size_t len = strlen(upstream);
  while (upstream[--len] == '/')
    upstream[len] = '\0';

  // upstream may contain port at the end, parsing it
  // else using if any http protocol is specificed
  // otherwise using FALLBACK_UPSTREAM_PORT
  char *colon = upstream, *port = NULL, *tmp = NULL, *revert = NULL;
  while ((tmp = strchr(colon, ':')))
    colon = ++tmp;

  if (colon == upstream) // no : found
    port = FALLBACK_UPSTREAM_PORT;
  else {
    --colon; // now colon is at ':', makes reasoning a little easier
    if (colon[1] == '/') {  // the colon is followed after http or https
      if (colon[-1] == 's') // https
        port = "443";
      else // http
        port = "80";
    } else { // the colon is followed by the port at the end
      *colon =
          '\0'; // now the port and origin are separated by a null terminator
      revert = colon; // need to revert it back to the original format to be
                      // able to compare to the host header of the request
      port = ++colon;
    }
  }

  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;

  int status = 0;
  // targetting port based on the protocol of the upstream
  if ((status = getaddrinfo(upstream, port, &hints, &upstream_addrinfo)) != 0)
    return err("getaddrinfo", gai_strerror(status));

  // getaddrinfo() makes system calls that may sometime set errno, even if the
  // getaddrinfo returns 0
  errno = 0;

  if (revert)
    *revert = ':';

  return true;
}

bool connect_upstream(int *upstream_fd) {
  struct addrinfo *current = upstream_addrinfo;
  if (!current)
    return err("verify_upstream", "Upstream address info is NULL");

  do {
    if ((*upstream_fd = socket(current->ai_family, current->ai_socktype,
                               current->ai_protocol)) == -1)
      continue;

    // Have to setsocketopt to allow dual-stack setup supporting both IPv4 &
    // v6
    if (setsockopt(*upstream_fd, IPPROTO_IPV6, IPV6_V6ONLY, &(int){0},
                   sizeof(int)) == -1) {
      *upstream_fd = -2;
      goto close;
    }

    if (connect(*upstream_fd, current->ai_addr, current->ai_addrlen) == -1) {
      *upstream_fd = -3;
      goto close;
    }

    // If a valid socket is connected to then break
    break;

  close:
    close(*upstream_fd);
    continue;
  } while ((current = current->ai_next));

  // dealing with different errors
  if (*upstream_fd < 0) {
    if (*upstream_fd == -1)
      return err("socket", strerror(errno));
    else if (*upstream_fd == -2)
      return err("setsockopt", strerror(errno));
    else if (*upstream_fd == -3)
      return err("connect", strerror(errno));
  }

  if (!set_non_block(*upstream_fd))
    return err("set_non_block", NULL);

  return true;
}

void free_upstream_addrinfo(void) {
  if (upstream_addrinfo)
    freeaddrinfo(upstream_addrinfo);
}

bool write_request(Connection *conn) {
  if (!conn)
    return set_efault();

  assert(conn->state == WRITE_REQUEST);

  Endpoint *client = &conn->client, *upstream = &conn->upstream;

  // writing request headers from client buffer to upstream
  upstream->to_write = client->buf_view.len - upstream->write_index;
  ssize_t write_status = 0;

  while ((upstream->to_write -= write_status) &&
         (write_status =
              write(upstream->fd, client->buffer + upstream->write_index,
                    upstream->to_write)) > 0)
    upstream->write_index += write_status;

  if (!write_status)
    return err("write", "No write status");

  if (write_status == -1) {
    if (errno == EINTR && !RUNNING) // shutdown
      NULL;
    else if (errno == EAGAIN || errno == EWOULDBLOCK) // cannot write now
      NULL;
    else
      return err("write", strerror(errno));
  }

  return true;
}
