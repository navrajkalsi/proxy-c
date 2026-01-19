#include <assert.h>
#include <errno.h>
#include <openssl/ssl.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "client.h"
#include "connection.h"
#include "proxy.h"
#include "utils.h"

void accept_client(void)
{
  assert(PROXY_FD >= 0);

  // looping, as epoll might be waken up by multiple incoming requests
  while (RUNNING)
  {
    Connection *conn = NULL;

    if (!(conn = init_conn()))
      return (void)err("init_conn", NULL);

    socklen_t addr_len = sizeof conn->client_addr;

    if ((conn->client.fd = accept(PROXY_FD, (struct sockaddr *)&conn->client_addr, &addr_len)) ==
        -1)
    {
      free_conn(&conn);

      if (errno == EINTR && !RUNNING) // shutdown
        break;

      if (errno == EAGAIN || errno == EWOULDBLOCK) // no more connections / data
        break;

      if (errno == ECONNABORTED) // client aborted
      {
        warn("accept", strerror(errno));
        continue;
      }
    }

    set_non_block(conn->client.fd);

    // new conn should start by checking the first bytes of client request
    conn->state = PEEK_CLIENT;
    add_to_epoll(conn, conn->client.fd, READ_FLAGS);
  }
}

void peek_client(Connection *conn)
{
  assert(conn);
  assert(conn->state == PEEK_CLIENT);

  Endpoint *client = &conn->client;

  // peek at first 3 bytes
  char tls_hello[3];

  ssize_t status = recv(client->fd, tls_hello, sizeof tls_hello, MSG_PEEK);
  bool encrypted = true;

  if (status == -1)
  {
    if (errno == EAGAIN || errno == EWOULDBLOCK) // should not block as reading right after epollin
      NULL;
    if (errno == EINTR && !RUNNING) // shutdown
      return;
    err("recv", strerror(errno));
    goto error;
  }

  // should atleast get 3 bytes
  // cannot epollin again, have to drain the buffer or switch to level triggered
  // which would be too complex for such a trivial task
  if (status < 3)
  {
    err("verify_hello_len", "Too few bytes received");
    goto error;
  }

  if (*tls_hello == 0x16 && tls_hello[1] == 0x03 && tls_hello[2] <= 0x03)
    encrypted = true;
  else if (*tls_hello >= 'A' && *tls_hello <= 'Z')
    encrypted = false;
  else
  {
    err("verify_client_hello", "Malformed Request");
    goto error;
  }

  if (encrypted)
  {
    if (!config.client_https)
    {
      err("client_tls", "Client TLS not setup, but received an encrypted request");
      goto error;
    }
    conn->state = TLS_CLIENT;
  }
  else
    // issue redirect for http to https only after parsing request line in read_request
    conn->state = READ_REQUEST;

  return;

error:
  conn->state = CLOSE_CONN;
}

void read_request(Connection *conn)
{
  assert(conn && conn->state == READ_REQUEST);

  Endpoint *client = &conn->client;

  // should not have next index, reset by reset_conn()
  assert(!client->next_index);

  ssize_t read_status = 0;
  size_t max_read = BUFFER_SIZE - (size_t)client->read_index;

  // new request should always start from the beginning of the buffer
  // client ssl is setup if config.client_https is enabled and the request was detected to be
  // encrypted
  while (
      (max_read -= (size_t)read_status) &&
      (read_status = client->ssl
                         ? SSL_read(client->ssl, client->buffer + client->read_index, (int)max_read)
                         : read(client->fd, client->buffer + client->read_index, max_read)) > 0)
  {
    if (!client->headers_found)
    {
      assert(!client->content_len);
      assert(!client->chunked);
      assert(!client->chunk_tracker.view_len);
      assert(!client->chunk_tracker.empty_found);
      // keep headers intact, reject body
      client->read_index += read_status;

      Str tmp_head = {.data = client->buffer, .len = client->read_index};
      if (!find_empty_line(&tmp_head))
      {
        if ((size_t)client->read_index == BUFFER_SIZE)
        { // headers too large
          err("read_request", "Headers too large");
          conn->status = 431;
          goto error;
        }
        continue;
      }
      client->head = tmp_head;

      if (!parse_head(conn, client))
      {
        err("parse_head", NULL);
        goto error;
      }

      print_request(conn);

      if (!client->ssl && config.client_https)
      { // issue https redirect here
        warn("verify_protocol", "Upgrading to HTTPS");
        conn->status = 301;
        goto error;
      }

      if (!verify_headers(conn, client))
      {
        err("verify_headers", NULL);
        goto error;
      }

      if (!check_body(conn, client))
      {
        err("check_body", NULL);
        goto error;
      }

      if (!client->to_read)
      {
        if (client->read_index > client->head.len)
          client->next_index = client->head.len;
        goto upstream;
      }
    }
    else if (client->content_len)
    {
      bool extra = client->to_read < (size_t)read_status;

      if (extra)
      {
        client->next_index = client->head.len + (ptrdiff_t)client->to_read;
        client->to_read = 0;
      }
      else
        client->to_read -= (size_t)read_status;

      if (!client->to_read)
        goto upstream;
    }
    else if (client->chunked)
    {
      Str body = {client->buffer + client->head.len, client->read_index - client->head.len};
      if (!handle_chunked(body, conn, client))
      {
        err("handle_chunked", NULL);
        goto error;
      }

      if (client->chunk_tracker.empty_found)
        goto upstream;
    }
    else
      assert(false); // logic error

    if (client->headers_found) // discard body if headers found, keep head intact
      client->read_index = client->head.len;
  }

  if (read_status <= 0 && client->ssl)
  {
    switch (SSL_get_error(client->ssl, (int)read_status))
    {
    case SSL_ERROR_WANT_READ:
      conn->prev_state = conn->state;
      conn->state = SSL_READ;
      return;
    case SSL_ERROR_WANT_WRITE:
      conn->prev_state = conn->state;
      conn->state = SSL_WRITE;
      return;
    case SSL_ERROR_ZERO_RETURN:
      warn("read", "Client EOF received");
      break;
    }
    conn->state = CLOSE_CONN; // close for all ssl_errors, except non-blocking ones
    return;
  }

  if (read_status == 0)
  { // client disconnect for read()
    conn->state = CLOSE_CONN;
    return;
  }

  if (read_status == -1)
  { // read() errors
    if (errno == EINTR && !RUNNING)
      return;
    else if (errno == EAGAIN || errno == EWOULDBLOCK)
      return;
    else
    {
      err("read", strerror(errno));
      conn->status = 500;
      goto error;
    }
  }

  return;

error:
  assert(conn->status >= 300);
  conn->state = WRITE_ERROR;
  return;

upstream:
  if (conn->upstream.fd < 0)
    conn->state = CONNECT_UPSTREAM;
  else
    conn->state = WRITE_REQUEST;
  return;
}

void write_request(Connection *conn)
{
  assert(conn);
  assert(conn->state == WRITE_REQUEST);

  Endpoint *client = &conn->client, *upstream = &conn->upstream;

  // writing request headers from client buffer to upstream
  client->to_write = (size_t)(client->head.len - client->write_index);
  ssize_t write_status = 0;

  while ((client->to_write -= (size_t)write_status) &&
         (write_status = upstream->ssl
                             ? SSL_write(upstream->ssl, client->buffer + client->write_index,
                                         (int)client->to_write)
                             : write(upstream->fd, client->buffer + client->write_index,
                                     client->to_write)) > 0)
    client->write_index += write_status;

  if (write_status <= 0 && upstream->ssl)
  {
    switch (SSL_get_error(client->ssl, (int)write_status))
    {
    case SSL_ERROR_WANT_READ:
      conn->prev_state = conn->state;
      conn->state = SSL_READ;
      return;
    case SSL_ERROR_WANT_WRITE:
      conn->prev_state = conn->state;
      conn->state = SSL_WRITE;
      return;
    case SSL_ERROR_ZERO_RETURN:
      err("SSL_write", "No write status");
      break;
    }
    conn->state = CLOSE_CONN; // close for all ssl_errors, except non-blocking ones
    return;
  }

  if (!write_status) // from regular write call
  {
    err("write", "No write status");
    goto error;
  }

  if (write_status == -1)
  {
    if (errno == EINTR && !RUNNING) // shutdown
      return;
    else if (errno == EAGAIN || errno == EWOULDBLOCK) // cannot write now
      return;
    else
    {
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
