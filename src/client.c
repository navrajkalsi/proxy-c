#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <openssl/ssl.h>
#include <regex.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "client.h"
#include "connection.h"
#include "http.h"
#include "proxy.h"
#include "str.h"
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

    // new conn should start with TLS_CLIENT
    conn->state = TLS_CLIENT;
    handle_state(conn);
  }
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

      if ((size_t)client->read_index == BUFFER_SIZE)
      { // headers too large
        err("read_request", "Headers too large");
        conn->status = 431;
        goto error;
      }

      Str tmp_head = {.data = client->buffer, .len = client->read_index};
      if (!find_empty_line(&tmp_head)) // full headers found
        continue;
      client->head = tmp_head;

      if (!parse_head(conn, client))
      {
        err("parse_head", NULL);
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
        goto connect_upstream;
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
        goto connect_upstream;
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
        goto connect_upstream;
    }
    else
      assert(false); // logic error

    if (client->headers_found) // discard body if headers found, keep head intact
      client->read_index = client->head.len;
  }

  if (read_status == 0)
  { // client disconnect
    conn->state = CLOSE_CONN;
    warn("read", "Client EOF received");
    return;
  }

  if (read_status == -1)
  {
    if (errno == EINTR && !RUNNING) // shutdown
      NULL;
    else if (errno == EAGAIN || errno == EWOULDBLOCK) // no more data right now
      NULL;
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

connect_upstream:
  conn->state = CONNECT_UPSTREAM;
  return;
}

void write_request(Connection *conn)
{
  if (!conn)
    goto error;

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

  if (!upstream->to_write) // wait for upstream response, if request is sent
    conn->state = READ_RESPONSE;
  return;

error:
  conn->status = 500;
  conn->state = WRITE_ERROR;
  return;
}
