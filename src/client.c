#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
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

void accept_client(int proxy_fd) {
  // looping, as epoll might be waken up by multiple incoming requests
  while (RUNNING) {
    Connection *conn = NULL;

    if (!(conn = init_conn())) {
      err("init_conn", NULL);
      break;
    }

    socklen_t addr_len = sizeof conn->client_addr;

    if ((conn->client.fd =
             accept(proxy_fd, (struct sockaddr *)&conn->client_addr,
                    &addr_len)) == -1) {

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

    if (!set_non_block(conn->client.fd)) {
      free_conn(&conn);
      err("set_non_block", NULL);
      continue;
    }

    if (!add_to_epoll(conn, conn->client.fd, READ_FLAGS)) {
      free_conn(&conn);
      err("add_to_epoll", NULL);
      continue;
    }
    // now the new client will be accepted to make a request
  }

  return;
}

void read_request(Connection *conn) {
  if (!conn)
    goto error;

  assert(conn->state == READ_REQUEST);

  Endpoint *client = &conn->client;

  // in case continuing to read after dealing with previous request
  if (client->next_index)
    if (!pull_buf(client)) {
      err("pull_buf", strerror(errno));
      goto error;
    }

  ssize_t read_status = 0;

  // new request should always start from the beginning of the buffer
  while (client->to_read &&
         (read_status = read(client->fd, client->buffer + client->read_index,
                             client->to_read)) > 0) {

    client->buffer[client->read_index + read_status] = '\0';
    puts(client->buffer);

    // if chunked then reject the body
    // else read upto to_read (which is maybe set by content len if headers were
    // found)
    client->to_read = client->chunked
                          ? (size_t)(BUFFER_SIZE - client->read_index - 1)
                          : client->to_read - read_status;

    if (!client->headers_found) {
      client->read_index += read_status;
      if (!verify_read(
              conn)) { // error response status set, send error response
        conn->state = WRITE_ERROR;
        break;
      }

      // no content len or encoding was specified
      if (client->headers_found && !client->to_read &&
          !client->chunked) { // request complete, now verify it
        conn->state = VERIFY_REQUEST;
        break;
      }
      continue;
    }

    if (client->chunked) { // checking for last chunk, was not received during
                           // verify_read()

      // remember, here read_index will be the index of headers_end (one byte
      // after last \n)
      // AND read_index has NOT been advanced by read_status

      if (find_last_chunk(client)) {
        conn->state = VERIFY_REQUEST;
        break;
      }
    }

    if (!client->to_read) // done reading body, set from content-len
      conn->state = VERIFY_REQUEST;
  }

  if (read_status == 0) { // client disconnect
    conn->state = CLOSE_CONN;
    err("read", "EOF received");
    return;
  }

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
  conn->state = WRITE_ERROR;
  conn->status = 500;
  return;
}

bool verify_read(Connection *conn) {
  if (!conn)
    return set_efault();

  Endpoint *client = &conn->client;

  // this function will determine if the request is compelete (got the headers
  // or not), and should not be used after the headers are read
  assert(!client->headers_found);
  assert(client->buffer[client->read_index] == '\0');

  char *headers_end = NULL;
  if (!(headers_end = strstr(client->buffer, TRAILER))) {
    if (client->read_index >= BUFFER_SIZE - 1) { // no space left
      conn->status = 431;
      return err("strstr", "Headers too large");
    }
    return true; // read more
  } else
    client->headers_found = true;

  headers_end += TRAILER_STR.len; // now past the last \n
  size_t headers_size = headers_end - client->buffer;

  client->headers.data = client->buffer;
  client->headers.len = headers_size;

  // tmp null termination for get_header_value(), so I do not get to the next
  // request or search for the header in the body (if read)
  char org_char = client->buffer[headers_size];
  client->buffer[headers_size] = '\0';

  Str misc = ERR_STR; // misc str to contain the header value
  if (get_header_value(client->buffer, "Content-Length", &misc)) {
    client->buffer[headers_size] = org_char;
    Str *content_len_str = &misc;

    for (int i = 0; i < content_len_str->len; i++)
      if (!isdigit(content_len_str->data[i])) {
        conn->status = 400;
        return err("isdigit", "Invalid content-length header value");
      }

    // a null terminated str for atoi()
    char *temp = strndup(content_len_str->data, content_len_str->len);
    int content_len = atoi(temp);
    free(temp);

    if (!content_len) // empty body
      goto read_complete;

    if (content_len > 10 * MB) {
      conn->status = 413;
      return err("verify_content_len", "Content too large");
    }

    size_t request_size = headers_size + content_len;

    if (request_size ==
        (size_t)client->read_index) // body read already, but nothing else
      goto read_complete;

    if (request_size <
        (size_t)client->read_index) { // body read and another request
      client->next_index =
          request_size + 1; // will be copied to the start for next read
      goto read_complete;
    }

    client->to_read = request_size - client->read_index;
    goto disregard_body;

  } else if (get_header_value(client->buffer, "Transfer-Encoding", &misc)) {
    client->buffer[headers_size] = org_char;
    Str *transfer_encoding = &misc;

    if (!equals(*transfer_encoding, STR("chunked"))) {
      conn->status = 411;
      return err("verify_encoding", "Encoding method not supported");
    }

    // finding full last chunk or partial from last few bytes
    // worst case, got: '0\r\n\r'
    if (find_last_chunk(client))
      goto read_complete;

    client->chunked = true;
    goto disregard_body;

  } else {
    client->buffer[headers_size] = org_char;
    goto read_complete;
  }

read_complete:
  client->to_read = 0;
  return true;

disregard_body:
  client->read_index = headers_size;
  return true;
}

bool verify_request(Connection *conn) {
  if (!conn)
    return set_efault();

  assert(conn->state == VERIFY_REQUEST);

  Endpoint *client = &conn->client;
  Cut c = cut(client->headers, ' ');

  // verifying method
  if (!c.found) {
    conn->status = 400;
    return err("validate_method", "Invalid request");
  } else if (!validate_method(c.head)) {
    conn->status = 405;
    return err("validate_method", "Invalid method");
  }

  // finding request path
  c = cut(c.tail, ' ');

  if (!c.found) {
    conn->status = 400;
    return err("validate_path", "Invalid request");
  }
  conn->path = c.head;

  // finding http version
  c = cut(c.tail, '\r');

  if (!c.found) {
    conn->status = 400;
    return err("validate_http", "Invalid request");
  } else if (!validate_http(c.head)) {
    conn->status = 500;
    return err("validate_http", "Invalid HTTP version");
  }
  conn->http_ver = c.head;

  // finding the host header
  if (!get_header_value(c.tail.data, "Host", &conn->host)) {
    conn->status = 400;
    return err("get_header_value", "Host header not found");
  }

  if (!validate_host(&conn->host)) {
    conn->status = 301;
    return err("validate_host", "Different host in the request header");
  }

  // respecting client connection, in case of no error
  set_connection(conn->client.buffer, conn);
  conn->status = 200;

  return true;
}

void handle_error_response(Connection *conn) {
  if (!conn)
    return;

  assert(conn->state == WRITE_ERROR);
  assert(conn->status && conn->status >= 300);

  // using upstream buffer as client buffer may contain another request, but
  // upstream buffer will be empty
  if (!generate_error_response(conn)) {
    err("generate_error_response", NULL);
    char temp_error[] = "500 Internal Server Error";
    memcpy(conn->upstream.buffer, temp_error, sizeof temp_error);
  }

  if (!write_error_response(conn))
    err("write_error_response", NULL);

  // close connection header sent
  // for errors
  conn->state = CLOSE_CONN;
}

bool generate_error_response(Connection *conn) {
  if (!conn)
    return set_efault();

  assert(conn->status >= 300);

  Endpoint *upstream = &conn->upstream;

  char date[DATE_LEN] = {0};
  if (!set_date_string(date))
    return err("set_date_string", NULL);

  const Str err_str = get_status_str(conn->status),
            date_str = {.data = date, .len = DATE_LEN - 1},
            response_body[] = {
                STR("<html><head><title>"), err_str,
                STR("</title</head><body><center><h1>"), err_str,
                STR("</h1></center><hr>center>" SERVER "</center></body>")};

  size_t body_elms = sizeof response_body / sizeof(Str), body_size = 0;

  for (uint i = 0; i < body_elms; ++i)
    body_size += response_body[i].len;

  // calculating number of bytes required to hold the final length
  // mostly will be 3, still checking
  int divisor = 1, num_of_digits = 0;
  while (body_size / divisor > 0 && ++num_of_digits)
    divisor *= 10;

  char content_len_data[num_of_digits];
  memset(content_len_data, 0, num_of_digits);

  int_to_string(body_size, content_len_data);
  if (!*content_len_data)
    return err("int_to_string", NULL);

  const Str content_length = {.data = content_len_data, .len = num_of_digits},
            location = {.data = config.canonical_host,
                        .len = (ptrdiff_t)strlen(config.canonical_host)},
            response_headers[] = {
                STR(FALLBACK_HTTP_VER),
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
    headers_size += response_headers[i].len;

  if (headers_size + body_size > BUFFER_SIZE)
    return err("collect_response", "Error response too big");

  ptrdiff_t buf_ptr = 0;

  for (uint i = 0; i < header_elms; ++i) {
    if (!response_headers[i].len) // skip if ERR_STR
      continue;
    memcpy(upstream->buffer + buf_ptr, response_headers[i].data,
           response_headers[i].len);
    buf_ptr += response_headers[i].len;
  }
  for (uint i = 0; i < body_elms; ++i) {
    memcpy(upstream->buffer + buf_ptr, response_body[i].data,
           response_body[i].len);
    buf_ptr += response_body[i].len;
  }
  upstream->buffer[buf_ptr] = '\0';
  upstream->to_write = (size_t)buf_ptr;

  return true;
}

bool write_error_response(Connection *conn) {
  if (!conn)
    return set_efault();

  Endpoint *upstream = &conn->upstream;

  ssize_t write_status = 0;

  while ((upstream->to_write -= write_status) &&
         (write_status =
              write(conn->client.fd, upstream->buffer + upstream->write_index,
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

bool write_str(const Connection *conn, const Str *str) {
  if (!conn || !str)
    return set_efault();

  ptrdiff_t current = 0;

  while (str->len && current < str->len) {
    long wrote = write(conn->client.fd, str->data + current,
                       (size_t)(str->len - current));
    if (wrote < 0)
      return err("write", strerror(errno));
    current += wrote;
  }

  return true;
}

void write_response(Connection *conn) {
  if (!conn)
    goto error;

  assert(conn->state == WRITE_RESPONSE);

  Endpoint *client = &conn->client, *upstream = &conn->upstream;

  // reset to begin again
  if (!client->to_write)
    client->write_index = 0;

  client->to_write =
      upstream->read_index - upstream->next_index - client->write_index;
  ssize_t write_status = 0;

  while (
      (client->to_write -= write_status) &&
      (write_status = write(client->fd, upstream->buffer + client->write_index,
                            client->to_write)) > 0)
    client->write_index += write_status;

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

  if (!client->to_write && !conn->complete)
    conn->state = READ_RESPONSE;

  if (conn->complete)
    conn->state = CLOSE_CONN;

  return;

error:
  conn->status = 500;
  conn->state = WRITE_ERROR;
  return;
}
