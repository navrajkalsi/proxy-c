#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "args.h"
#include "connection.h"
#include "http.h"
#include "main.h"
#include "upstream.h"
#include "utils.h"

// global var that stores a linked list of struct addrinfo containing info about
// upstream server
struct addrinfo *upstream_addrinfo = NULL;

bool setup_upstream(char *upstream)
{
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
  else
  {
    --colon; // now colon is at ':', makes reasoning a little easier
    if (colon[1] == '/')
    { // the colon is followed after http or https
      if (colon[-1] == 's')
        port = "443";
      else
        port = "80";
    }
    else
    {                 // the colon is followed by the port at the end
      *colon = '\0';  // now the port and origin are separated by a null terminator
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

  // getaddrinfo() makes system calls that may sometime set errno, even if getaddrinfo returns 0
  errno = 0;

  if (revert)
    *revert = ':';

  return true;
}

bool connect_upstream(int *upstream_fd)
{
  if (!upstream_addrinfo)
    return err("verify_upstream", "Upstream address info is NULL");

  for (struct addrinfo *current = upstream_addrinfo; current; current = current->ai_next)
  {
    if ((*upstream_fd = socket(current->ai_family, current->ai_socktype, current->ai_protocol)) ==
        -1)
      continue;

    if (connect(*upstream_fd, current->ai_addr, current->ai_addrlen) == -1)
    {
      close(*upstream_fd);
      *upstream_fd = -2;
      continue;
    }

    // If a valid socket is connected to then break
    break;
  };

  if (*upstream_fd < 0)
  { // dealing with different errors
    if (*upstream_fd == -1)
      return err("socket", strerror(errno));
    else if (*upstream_fd == -2)
      return err("connect", strerror(errno));
  }

  if (!set_non_block(*upstream_fd))
    return err("set_non_block", NULL);

  return true;
}

void free_upstream_addrinfo(void)
{
  if (upstream_addrinfo)
    freeaddrinfo(upstream_addrinfo);
}

void read_response(Connection *conn)
{
  if (!conn)
    goto error;

  assert(conn->state == READ_RESPONSE);
  assert(!conn->complete);

  Endpoint *upstream = &conn->upstream;

  // should not have next index, reset by reset_conn()
  if (upstream->next_index)
  {
    err("verify_next_index", "Next index not reset. Logic error!");
    goto error;
  }

  // after finding the headers
  // must have written the buffer to client in full, before reading again
  if (upstream->headers_found)
  {
    upstream->read_index = 0;
    *upstream->buffer = '\0';
  }

  ssize_t read_status = 0;
  size_t max_read = BUFFER_SIZE - (size_t)upstream->read_index - 1;

  while ((max_read -= (size_t)read_status) &&
         (read_status = read(upstream->fd, upstream->buffer + upstream->read_index, max_read)) > 0)
  {
    {
      upstream->read_index += read_status;
      upstream->buffer[upstream->read_index] = '\0';
    }

    if (!upstream->headers_found)
    {
      if (!parse_headers(conn, upstream))
        goto error;

      // no content len or encoding was specified or full response read
      if (upstream->headers_found && !upstream->to_read)
        goto complete;
    }
    else if (upstream->content_len)
    { // bytes left from content len
      size_t extra =
          (size_t)read_status > upstream->to_read ? (size_t)read_status - upstream->to_read : 0;

      if (extra)
      {
        upstream->to_read = 0;
        upstream->next_index = upstream->read_index - (ptrdiff_t)extra;
      }
      else
        upstream->to_read -= (size_t)read_status;

      if (!upstream->to_read)
        goto complete;
    }
    else if (upstream->chunked)
    { // checking for last chunk, was not received during parse_headers()
      if (find_last_chunk(upstream))
        goto complete;
    }
    else
    {
      err("verify_upstream_read", "No read condition met. Logic error!");
      goto error;
    }
  }

  if (read_status == 0)
  { // upstream disconnect
    conn->state = CLOSE_CONN;
    err("read", "Upstream EOF received");
    return;
  }

  // write whats in buffer, only if headers are found so that
  // parse_headers can work (in case of partial header reads)
  // else continue to read more
  if (upstream->headers_found)
    conn->state = WRITE_RESPONSE;

  if (read_status == -1)
  {
    if (errno == EINTR && !RUNNING) // shutdown
      NULL;
    else if (errno == EAGAIN || errno == EWOULDBLOCK) // no more data right now
      NULL;
    else
    {
      err("read", strerror(errno));
      goto error;
    }
  }

  return;

complete:
  conn->complete = true;
  conn->state = WRITE_RESPONSE;
  return;

error:
  conn->status = 500;
  conn->state = WRITE_ERROR;
  return;
}

void handle_error_response(Connection *conn)
{
  if (!conn)
    return;

  assert(conn->state == WRITE_ERROR);
  assert(conn->status && conn->status >= 300);

  // every thing response related should use upstream vars
  if (!generate_error_response(conn))
  {
    err("generate_error_response", NULL);
    char tmp_err[] = "500 Internal Server Error";
    memcpy(conn->upstream.buffer, tmp_err, sizeof tmp_err);
  }

  if (!write_error_response(conn))
    err("write_error_response", NULL);

  // close connection header sent for errors
  conn->state = CLOSE_CONN;
}

bool generate_error_response(Connection *conn)
{
  if (!conn)
    return set_efault();

  assert(conn->status >= 300);

  Endpoint *upstream = &conn->upstream;

  char date[DATE_LEN] = {0};
  if (!set_date_string(date))
    return err("set_date_string", NULL);

  const Str err_str = get_status_str(conn->status), date_str = {.data = date, .len = DATE_LEN - 1},
            response_body[] = {STR("<html><head><title>"), err_str,
                               STR("</title></head><body><center><h1>"), err_str,
                               STR("</h1></center><hr><center>" SERVER "</center></body></html>")};

  size_t body_elms = sizeof response_body / sizeof(Str), body_size = 0;

  for (uint i = 0; i < body_elms; ++i)
    body_size += (size_t)response_body[i].len;

  // calculating number of chars required to hold the final length, will mostly be 3
  uint divisor = 1, num_of_digits = 0;
  while (body_size / divisor > 0 && ++num_of_digits)
    divisor *= 10;

  char content_len_data[num_of_digits];
  memset(content_len_data, 0, num_of_digits);

  int_to_string((int)body_size, content_len_data);
  if (!*content_len_data)
    return err("int_to_string", NULL);

  const Str content_length = {.data = content_len_data, .len = num_of_digits},
            location = {.data = config.canonical_host,
                        .len = (ptrdiff_t)strlen(config.canonical_host)},
            response_headers[] = {STR(FALLBACK_HTTP_VER),
                                  SPACE_STR,
                                  err_str,
                                  STR("\r\nServer: " SERVER "\r\nDate: "),
                                  date_str,
                                  STR("\r\nContent-Type: text/html\r\nContent-Length: "),
                                  content_length,
                                  STR("\r\nConnection: "),
                                  STR("close"), // close for errors
                                  conn->status < 400 ? STR("\r\nLocation: ")
                                                     : ERR_STR, // location only for redirections
                                  conn->status < 400 ? location : ERR_STR,
                                  STR("\r\n\r\n")};

  // collecting all response in upstream_buffer
  size_t header_elms = sizeof response_headers / sizeof(Str), headers_size = 0;
  for (uint i = 0; i < header_elms; ++i)
    headers_size += (size_t)response_headers[i].len;

  if (headers_size + body_size > BUFFER_SIZE)
    return err("collect_response", "Error response too big");

  ptrdiff_t buf_ptr = 0;

  for (uint i = 0; i < header_elms; ++i)
  {
    if (!response_headers[i].len) // skip if ERR_STR
      continue;
    memcpy(upstream->buffer + buf_ptr, response_headers[i].data, (size_t)response_headers[i].len);
    buf_ptr += response_headers[i].len;
  }
  for (uint i = 0; i < body_elms; ++i)
  {
    memcpy(upstream->buffer + buf_ptr, response_body[i].data, (size_t)response_body[i].len);
    buf_ptr += response_body[i].len;
  }
  upstream->buffer[buf_ptr] = '\0';
  upstream->to_write = (size_t)buf_ptr;

  return true;
}

bool write_error_response(Connection *conn)
{
  if (!conn)
    return set_efault();

  Endpoint *upstream = &conn->upstream;

  ssize_t write_status = 0;

  while ((upstream->to_write -= (size_t)write_status) &&
         (write_status = write(conn->client.fd, upstream->buffer + upstream->write_index,
                               upstream->to_write)) > 0)
    upstream->write_index += write_status;

  if (!write_status)
    return err("write", "No write status");

  if (write_status == -1)
  {
    if (errno == EINTR && !RUNNING) // shutdown
      NULL;
    else if (errno == EAGAIN || errno == EWOULDBLOCK) // cannot write now
      NULL;
    else
      return err("write", strerror(errno));
  }

  return true;
}

void write_response(Connection *conn)
{
  if (!conn)
    goto error;

  assert(conn->state == WRITE_RESPONSE);

  Endpoint *client = &conn->client, *upstream = &conn->upstream;

  // reset to begin again
  if (!upstream->to_write)
    upstream->write_index = 0;

  upstream->to_write =
      (size_t)((upstream->next_index ? upstream->next_index : upstream->read_index) -
               upstream->write_index);
  ssize_t write_status = 0;

  while ((upstream->to_write -= (size_t)write_status) &&
         (write_status =
              write(client->fd, upstream->buffer + upstream->write_index, upstream->to_write)) > 0)
    upstream->write_index += write_status;

  if (!write_status)
  {
    err("write", "No write status");
    goto error;
  }

  if (write_status == -1)
  {
    if (errno == EINTR && !RUNNING) // shutdown
      NULL;
    else if (errno == EAGAIN || errno == EWOULDBLOCK) // cannot write now
      NULL;
    else
    {
      err("write", strerror(errno));
      goto error;
    }
  }

  // if (!upstream->to_write && !conn->complete)
  //   conn->state = READ_RESPONSE;

  if (conn->complete)
    conn->state = CHECK_CONN;
  else
    conn->state = READ_RESPONSE;

  return;

error:
  conn->status = 500;
  conn->state = WRITE_ERROR;
  return;
}
