#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <unistd.h>

#include "connection.h"
#include "main.h"
#include "proxy.h"
#include "upstream.h"
#include "utils.h"

// global var that stores a linked list of struct addrinfo containing info about upstream server
static struct addrinfo *upstream_addrinfo = NULL;

bool setup_upstream(void)
{
  Str host = config.upstream_host.host, port = config.upstream_host.port;
  char host_local[host.len + 1], port_local[6];

  assert(host.len);

  // null terminate host
  memcpy(host_local, host.data, (size_t)host.len);
  host_local[host.len] = '\0';

  if (port.len)
  {
    memcpy(port_local, port.data, (size_t)port.len);
    port_local[port.len] = '\0';
  }
  else if (config.upstream_https)
    memcpy(port_local, "443", 4);
  else
    memcpy(port_local, "80", 3);

  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  int status = 0;
  // targetting port based on the protocol of the upstream
  if ((status = getaddrinfo(host_local, port_local, &hints, &upstream_addrinfo)) != 0)
    return err("getaddrinfo", gai_strerror(status));

  // getaddrinfo() makes system calls that may sometime set errno, even if getaddrinfo returns 0
  errno = 0;

  return true;
}

void connect_upstream(Connection *conn)
{
  assert(upstream_addrinfo && conn);
  assert(conn->state == CONNECT_UPSTREAM);

  int upstream_fd = -1;

  for (struct addrinfo *current = upstream_addrinfo; current; current = current->ai_next)
  {
    if ((upstream_fd = socket(current->ai_family, current->ai_socktype, current->ai_protocol)) ==
        -1)
      continue;

    if (connect(upstream_fd, current->ai_addr, current->ai_addrlen) == -1)
    {
      close(upstream_fd);
      upstream_fd = -2;
      continue;
    }

    // If a valid socket is connected to then break
    break;
  };

  if (upstream_fd < 0)
  { // dealing with different errors
    if (upstream_fd == -1)
      err("socket", strerror(errno));
    else if (upstream_fd == -2)
      err("connect", strerror(errno));
    conn->status = 500;
    conn->state = WRITE_ERROR;
    return;
  }

  conn->upstream.fd = upstream_fd;

  set_non_block(upstream_fd);
  add_to_epoll(conn, upstream_fd, 0);

  if (config.upstream_https)
    conn->state = TLS_UPSTREAM;
  else
    conn->state = WRITE_REQUEST;
}

void free_upstream_addrinfo(void)
{
  if (upstream_addrinfo)
    freeaddrinfo(upstream_addrinfo);
}

void read_response(Connection *conn)
{
  assert(conn);
  assert(conn->state == READ_RESPONSE);
  assert(!conn->complete);

  Endpoint *upstream = &conn->upstream;

  // should not have next index, reset by reset_conn()
  assert(!upstream->next_index);

  // after finding the headers
  // must have written the buffer to client in full, before reading again
  if (upstream->headers_found)
    upstream->read_index = 0;

  ssize_t read_status = 0;
  size_t max_read = BUFFER_SIZE - (size_t)upstream->read_index;

  while ((max_read -= (size_t)read_status) &&
         (read_status =
              upstream->ssl
                  ? SSL_read(upstream->ssl, upstream->buffer + upstream->read_index, (int)max_read)
                  : read(upstream->fd, upstream->buffer + upstream->read_index, max_read)) > 0)
  {
    upstream->read_index += read_status;

    if (!upstream->headers_found)
    {
      assert(!upstream->content_len);
      assert(!upstream->chunked);
      assert(!upstream->chunk_tracker.view_len);
      assert(!upstream->chunk_tracker.empty_found);

      Str tmp_head = {.data = upstream->buffer, .len = upstream->read_index};
      if (!find_empty_line(&tmp_head))
      {
        if ((size_t)upstream->read_index == BUFFER_SIZE)
        { // headers too large
          err("read_request", "Headers too large");
          conn->status = 502;
          goto error;
        }
        continue;
      }
      upstream->head = tmp_head;

      if (!parse_head(conn, upstream))
      {
        err("parse_head", NULL);
        goto error;
      }

      if (!verify_headers(conn, upstream))
      {
        err("verify_headers", NULL);
        goto error;
      }

      if (!check_body(conn, upstream))
      {
        err("check_body", NULL);
        goto error;
      }

      // no content len or encoding was specified or full response read
      if (!upstream->to_read)
        goto complete;
    }
    else if (upstream->content_len)
    { // bytes left from content len
      bool extra = upstream->to_read < (size_t)read_status;

      if (extra)
      {
        upstream->next_index = upstream->head.len + (ptrdiff_t)upstream->to_read;
        upstream->to_read = 0;
      }
      else
        upstream->to_read -= (size_t)read_status;

      if (!upstream->to_read)
        goto complete;
    }
    else if (upstream->chunked)
    { // checking for last chunk, was not received during parse_head()
      Str body = {upstream->buffer, upstream->read_index};
      if (!handle_chunked(body, conn, upstream))
      {
        err("handle_chunked", NULL);
        goto error;
      }

      if (upstream->chunk_tracker.empty_found)
        goto complete;
    }
    else
      assert(false);
  }

  if (read_status <= 0 && upstream->ssl)
  {
    int ssl_error = SSL_get_error(upstream->ssl, (int)read_status);
    if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE)
      return;
    else
    {
      if (ssl_error == SSL_ERROR_ZERO_RETURN)
        warn("SSL_read", "Upstream EOF received");
      conn->state = CLOSE_CONN; // close for all ssl_errors, except non-blocking
      return;
    }
  }

  if (read_status == 0)
  { // upstream disconnect
    conn->state = CLOSE_CONN;
    warn("read", "Upstream EOF received");
    return;
  }

  if (read_status == -1)
  {
    if (errno == EINTR && !RUNNING) // shutdown
      return;
    else if (errno == EAGAIN || errno == EWOULDBLOCK) // no more data right now
      return;
    else
    {
      err("read", strerror(errno));
      goto error;
    }
  }

  // write whats in buffer, only if headers are found
  // this is because parse_headers() requires all the headers to be present in one continuous memory
  // else continue to read more
  if (upstream->headers_found)
    conn->state = WRITE_RESPONSE;

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
  assert(conn);
  assert(conn->state == WRITE_ERROR);
  assert(conn->status >= 300);

  // every thing response related should use upstream vars
  if (!generate_error_response(conn))
  {
    err("generate_error_response", NULL);
    char tmp_err[] = "500 Internal Server Error";
    memcpy(conn->upstream.buffer, tmp_err, sizeof tmp_err); // atleast send the error code
  }

  if (!write_error_response(conn))
    err("write_error_response", NULL);

  // close connection header sent for errors
  conn->state = CLOSE_CONN;
}

bool generate_error_response(Connection *conn)
{
  assert(conn);
  assert(conn->status >= 300);

  Endpoint *upstream = &conn->upstream;

  char local[DATE_LEN] = {0};
  Str date = {local, DATE_LEN};

  if (!set_date_str(date))
    return err("set_date_str", NULL);
  date.len--; // discard the null terminator at the end

  Str err_str = get_status_str(conn->status),
      response_body[] = {STR("<html>\n<head><title>"), err_str,
                         STR("</title></head>\n<body>\n<center><h1>"), err_str,
                         STR("</h1></center>\n<hr><center>" SERVER "</center>\n</body>\n</html>")};

  size_t body_elms = sizeof response_body / sizeof(Str), body_size = 0;

  for (uint i = 0; i < body_elms; ++i)
    body_size += (size_t)response_body[i].len;
  assert(body_size);

  // calculating number of chars required to hold the final length, will mostly be 3
  uint divisor = 1, num_of_digits = 0;
  while (body_size / divisor > 0 && ++num_of_digits)
    divisor *= 10;

  char content_len_data[num_of_digits];
  memset(content_len_data, 0, num_of_digits);

  int_to_string((int)body_size, content_len_data);
  assert(*content_len_data); // must work, if num is > 0, which it is

  Str content_length = {.data = content_len_data, .len = num_of_digits},
      location = conn->status < 400 ? get_redirect_location(conn) : NULL_STR, // only generate on
                                                                              // redirections
      response_headers[] = {STR(DEFAULT_HTTP_VER),
                            SPACE,
                            err_str,
                            STR("\r\nServer: " SERVER "\r\nDate: "),
                            date,
                            STR("\r\nContent-Type: text/html\r\nContent-Length: "),
                            content_length,
                            STR("\r\nConnection: "),
                            STR("close"), // close for errors
                            conn->status < 400 ? STR("\r\nLocation: ")
                                               : NULL_STR, // location only for redirections
                            conn->status < 400 ? location : NULL_STR,
                            TRAILER};

  // collecting all response in upstream_buffer
  size_t header_elms = sizeof response_headers / sizeof(Str), headers_size = 0;
  for (uint i = 0; i < header_elms; ++i)
    headers_size += (size_t)response_headers[i].len;

  if (headers_size + body_size > BUFFER_SIZE)
    return err("collect_response", "Error response too big");

  ptrdiff_t buf_ptr = 0;

  for (uint i = 0; i < header_elms; ++i)
  {
    if (!response_headers[i].len) // skip if NULL_STR
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
  assert(conn);

  Endpoint *client = &conn->client, *upstream = &conn->upstream;

  ssize_t write_status = 0;

  while ((upstream->to_write -= (size_t)write_status) &&
         (write_status = client->ssl
                             ? SSL_write(client->ssl, upstream->buffer + upstream->write_index,
                                         (int)upstream->to_write)
                             : write(client->fd, upstream->buffer + upstream->write_index,
                                     upstream->to_write)) > 0)
    upstream->write_index += write_status;

  if (write_status <= 0 && client->ssl)
  {
    int ssl_error = SSL_get_error(client->ssl, (int)write_status);
    if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE)
      return true;
    else
    {
      if (ssl_error == SSL_ERROR_ZERO_RETURN)
        err("SSL_write", "No write status");
      conn->state = CLOSE_CONN; // close for all ssl_errors, except non-blocking
      return false;
    }
  }

  if (!write_status)
    return err("write", "No write status");

  if (write_status == -1)
  {
    if (errno == EINTR && !RUNNING) // shutdown
      return true;
    else if (errno == EAGAIN || errno == EWOULDBLOCK) // cannot write now
      return true;
    else
      return err("write", strerror(errno));
  }

  return true;
}

void write_response(Connection *conn)
{
  assert(conn);
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
         (write_status = client->ssl
                             ? SSL_write(client->ssl, upstream->buffer + upstream->write_index,
                                         (int)upstream->to_write)
                             : write(client->fd, upstream->buffer + upstream->write_index,
                                     upstream->to_write)) > 0)
    upstream->write_index += write_status;

  if (write_status <= 0 && client->ssl)
  {
    int ssl_error = SSL_get_error(client->ssl, (int)write_status);
    if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE)
      return;
    else
    {
      if (ssl_error == SSL_ERROR_ZERO_RETURN)
        err("SSL_write", "No write status");
      conn->state = CLOSE_CONN; // close for all ssl_errors, except non-blocking
      return;
    }
  }

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
