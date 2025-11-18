#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
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
  hints.ai_family = AF_UNSPEC;
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
  if (!upstream_addrinfo)
    return err("verify_upstream", "Upstream address info is NULL");

  for (struct addrinfo *current = upstream_addrinfo; current;
       current = current->ai_next) {
    if ((*upstream_fd = socket(current->ai_family, current->ai_socktype,
                               current->ai_protocol)) == -1)
      continue;

    if (connect(*upstream_fd, current->ai_addr, current->ai_addrlen) == -1) {
      close(*upstream_fd);
      *upstream_fd = -2;
      continue;
    }

    // If a valid socket is connected to then break
    break;
  };

  // dealing with different errors
  if (*upstream_fd < 0) {
    if (*upstream_fd == -1)
      return err("socket", strerror(errno));
    else if (*upstream_fd == -2)
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

void write_request(Connection *conn) {
  if (!conn)
    goto error;

  assert(conn->state == WRITE_REQUEST);

  Endpoint *client = &conn->client, *upstream = &conn->upstream;

  // writing request headers from client buffer to upstream
  upstream->to_write = client->headers.len - upstream->write_index;
  ssize_t write_status = 0;

  while ((upstream->to_write -= write_status) &&
         (write_status =
              write(upstream->fd, client->buffer + upstream->write_index,
                    upstream->to_write)) > 0)
    upstream->write_index += write_status;

  if (!write_status) {
    err("write", "No write status");
    goto error;
  }

  if (write_status == -1) {
    if (errno == EINTR && !RUNNING) // shutdown
      NULL;
    else if (errno == EAGAIN || errno == EWOULDBLOCK) // cannot write now
      NULL;
    else {
      err("write", strerror(errno));
      goto error;
    }
  }

  if (!upstream->to_write) // wait for upstream response, if request is sent
    conn->state = READ_RESPONSE;
  return;

error:
  conn->status = 500;
  conn->state = WRITE_ERROR;
  return;
}

void read_response(Connection *conn) {
  if (!conn)
    goto error;

  assert(conn->state == READ_RESPONSE);
  assert(!conn->complete);

  Endpoint *upstream = &conn->upstream;

  // in case continuing to read after dealing with previous response
  if (upstream->next_index)
    if (!pull_buf(upstream)) {
      err("pull_buf", strerror(errno));
      goto error;
    }

  // must have written the buffer to client in full, before reading again
  upstream->read_index = 0;

  ssize_t read_status = 0;
  size_t max_read =
      BUFFER_SIZE - upstream->read_index - 1; // most that can be read

  while (
      (upstream->to_read -= read_status) && (max_read -= read_status) &&
      (read_status = read(upstream->fd, upstream->buffer + upstream->read_index,
                          max_read)) > 0) {
    upstream->read_index += read_status;
    upstream->buffer[upstream->read_index] = '\0';
    // puts(upstream->buffer);

    if (!upstream->headers_found) {
      if (!parse_headers(conn, &conn->upstream))
        goto error;

      // no content len or encoding was specified
      if (upstream->headers_found && !upstream->to_read &&
          !upstream->chunked) { // response complete
        conn->complete = true;
        conn->state = WRITE_RESPONSE;
        break;
      }
      continue;
    }

    if (upstream->chunked) { // checking for last chunk, was not received during
                             // parse_headers()
      if (find_last_chunk(upstream)) {
        conn->complete = true;
        conn->state = WRITE_RESPONSE;
        break;
      }

      if (!max_read) {
        conn->state = WRITE_RESPONSE;
        break;
      }

      continue;
    }

    if (!upstream->to_read) { // done reading body, set from content-len or
                              // buffer is full
      if (max_read)           // is their space in buffer
        conn->complete = true;
      conn->state = WRITE_RESPONSE;
    }
  }

  if (read_status == 0) { // upstream disconnect
    conn->state = CLOSE_CONN;
    err("read", "EOF received");
    return;
  }

  conn->state = WRITE_RESPONSE; // write whats in buffer

  if (read_status == -1) {
    if (errno == EINTR && !RUNNING) // shutdown
      NULL;
    else if (errno == EAGAIN || errno == EWOULDBLOCK) // no more data right now
      NULL;
    else {
      err("read", strerror(errno));
      goto error;
    }
  }

  return;

error:
  conn->status = 500;
  conn->state = WRITE_ERROR;
  return;
}
