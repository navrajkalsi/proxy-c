#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
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
#include "main.h"
#include "utils.h"

void accept_client(int proxy_fd)
{
  // looping, as epoll might be waken up by multiple incoming requests
  while (RUNNING)
  {
    Connection *conn = NULL;

    if (!(conn = init_conn()))
    {
      err("init_conn", NULL);
      break;
    }

    socklen_t addr_len = sizeof conn->client_addr;

    if ((conn->client.fd = accept(proxy_fd, (struct sockaddr *)&conn->client_addr, &addr_len)) ==
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

    // TODO: think what if the client does not make a request forever

    if (!set_non_block(conn->client.fd))
    {
      free_conn(&conn);
      err("set_non_block", NULL);
      continue;
    }

    if (!add_to_epoll(conn, conn->client.fd, READ_FLAGS))
    {
      free_conn(&conn);
      err("add_to_epoll", NULL);
      continue;
    }
  }

  return;
}

void read_request(Connection *conn)
{
  if (!conn)
    goto error;

  assert(conn->state == READ_REQUEST);

  Endpoint *client = &conn->client;

  // in case continuing to read after dealing with previous request
  if (client->next_index)
    if (!pull_buf(client))
    {
      err("pull_buf", strerror(errno));
      goto error;
    }

  ssize_t read_status = 0;

  // new request should always start from the beginning of the buffer
  while (client->to_read &&
         (read_status = read(client->fd, client->buffer + client->read_index, client->to_read)) > 0)
  {
    client->buffer[client->read_index + read_status] = '\0';
    client->read_index += client->headers_found ? 0 : read_status;

    if (!client->headers_found)
    {
      if (!parse_headers(conn, client))
        goto error;

      // no content len or encoding was specified or full request read
      if (client->headers_found && !client->to_read)
        goto verify;
    }
    else if (client->content_len)
    { // bytes left from content len
      size_t extra =
          (size_t)read_status > client->to_read ? (size_t)read_status - client->to_read : 0;

      if (extra)
      {
        client->next_index = client->read_index + (ptrdiff_t)client->to_read;
        client->to_read = 0;
      }
      else
        client->to_read -= (size_t)read_status;

      if (!client->to_read)
        goto verify;
    }
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
    err("read", "EOF received");
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

verify:
  conn->state = VERIFY_REQUEST;
  return;

error:
  conn->state = WRITE_ERROR;
  conn->status = conn->status >= 300 ? conn->status : 500;
  return;
}

bool verify_request(Connection *conn)
{
  if (!conn)
    return set_efault();

  assert(conn->state == VERIFY_REQUEST);

  Endpoint *client = &conn->client;
  Cut c = cut(client->headers, ' ');

  // verifying method
  if (!c.found)
  {
    conn->status = 400;
    return err("validate_method", "Invalid request");
  }
  else if (!validate_method(c.head))
  {
    conn->status = 405;
    return err("validate_method", "Invalid method");
  }

  // finding request path
  c = cut(c.tail, ' ');

  if (!c.found)
  {
    conn->status = 400;
    return err("validate_path", "Invalid request");
  }
  conn->path = c.head;

  // finding http version
  c = cut(c.tail, '\r');

  if (!c.found)
  {
    conn->status = 400;
    return err("validate_http", "Invalid request");
  }
  else if (!validate_http(c.head))
  {
    conn->status = 500;
    return err("validate_http", "Invalid HTTP version");
  }
  conn->http_ver = c.head;

  // finding the host header
  if (!get_header_value(c.tail.data, "Host", &conn->host))
  {
    conn->status = 400;
    return err("get_header_value", "Host header not found");
  }

  if (!validate_host(&conn->host))
  {
    conn->status = 301;
    return err("validate_host", "Different host in the request header");
  }

  // respecting client connection, in case of no error
  set_connection(conn->client.buffer, conn);
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
  client->to_write = (size_t)(client->headers.len - client->write_index);
  ssize_t write_status = 0;

  while ((client->to_write -= (size_t)write_status) &&
         (write_status =
              write(upstream->fd, client->buffer + client->write_index, client->to_write)) > 0)
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
