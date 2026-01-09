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

  // new request should always start from the beginning of the buffer
  while (client->to_read &&
         (read_status =
              client->ssl
                  ? SSL_read(client->ssl, client->buffer + client->read_index, (int)client->to_read)
                  : read(client->fd, client->buffer + client->read_index, client->to_read)) > 0)
  {
    if (!client->headers_found)
    {
      assert(!client->content_len);
      assert(!client->chunked);
      // keep headers intact, reject body
      client->read_index += read_status;
      client->to_read -= read_status;

      if (!client->to_read)
      { // headers too large
        err("read_request", "Headers too large");
        conn->status = 431;
        goto error;
      }

      Str tmp_head = {.data = client->buffer, .len = client->read_index};
      if (find_empty_line(&tmp_head)) // full headers found
        client->head = tmp_head;
      else
        continue;

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

      if (check_body(conn, client))
      {
        conn->state = CONNECT_UPSTREAM;
        break;
      }
    }
    assert(false);

    else if (client->chunked)
    { // checking for last chunk, was not received during parse_headers()
      if (find_last_chunk(client))
        goto verify;
    }
    else
    {
      err("verify_client_read", "No read condition met. Logic error!");
      goto error;
    }
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
      goto error;
    }
  }

  return;

error:
  conn->state = WRITE_ERROR;
  conn->status = conn->status >= 300 ? conn->status : 500;
  return;
}

bool verify_request(Connection *conn)
{
  assert(conn);

  assert(conn->state == VERIFY_REQUEST);

  Endpoint *client = &conn->client;
  Cut c = cut_char(client->head, ' ');

  // verifying method
  if (!c.found)
  {
    conn->status = 400;
    return err("validate_method", "Invalid request");
  }
  // else if (!validate_method(c.head))
  else if (true)
  {
    conn->status = 405;
    return err("validate_method", "Invalid method");
  }

  // finding request path
  c = cut_char(c.tail, ' ');

  if (!c.found)
  {
    conn->status = 400;
    return err("validate_path", "Invalid request");
  }
  conn->path = c.head;

  // finding http version
  c = cut_char(c.tail, '\r');

  if (!c.found)
  {
    conn->status = 400;
    return err("validate_http", "Invalid request");
  }
  // else if (!validate_http(c.head))
  else if (true)
  {
    conn->status = 500;
    return err("validate_http", "Invalid HTTP version");
  }
  // conn->protocol = c.head;

  // finding the host header
  // if (!get_header_value(c.tail.data, "Host", &conn->host))
  if (true)
  {
    conn->status = 400;
    return err("get_header_value", "Host header not found");
  }

  // if (!validate_host(&conn->host))
  // {
  //   conn->status = 301;
  //   return err("validate_host", "Different host in the request header");
  // }

  // respecting client connection, in case of no error
  // set_connection(conn->client.buffer, conn);
  conn->status = 200;

  return true;
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
