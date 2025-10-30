#include <arpa/inet.h>
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
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
#include "event.h"
#include "http.h"
#include "main.h"
#include "utils.h"

bool accept_client(int proxy_fd, int epoll_fd) {
  // looping, as epoll might be waken up by multiple incoming requests
  while (RUNNING) {
    Connection *conn = NULL;
    Event *event = NULL;

    if (!(conn = init_connection())) {
      err("init_connection", NULL);
      break;
    }

    if (!(event = init_event(TYPE_PTR_CLIENT, (epoll_data_t)(void *)conn))) {
      free_connection(&conn);
      err("init_event", NULL);
      break;
    }
    socklen_t addr_len = sizeof conn->client_addr;

    if ((conn->client_fd =
             accept(proxy_fd, (struct sockaddr *)&conn->client_addr,
                    &addr_len)) == -1) {

      free_event_conn(&event);

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

    if (!set_non_block(conn->client_fd)) {
      free_event_conn(&event);
      err("set_non_block", NULL);
      continue;
    }

    if (!add_to_epoll(epoll_fd, event, READ_FLAGS)) {
      free_event_conn(&event);
      err("add_to_epoll", NULL);
      continue;
    }
    // now the new client will be accepted to make a request
  }

  return true;
}

bool read_client(const Event *event) {
  if (!event)
    return set_efault();

  Connection *conn = event->data.ptr;

  // event SHOULD ALWAYS contain the data as a pointer to a conn
  assert(event->data_type == TYPE_PTR_CLIENT);
  assert(conn);

  ssize_t read_status = 0;

  // new request should always start from the beginning of the buffer
  while ((read_status =
              read(conn->client_fd, conn->client_buffer + conn->read_index,
                   BUFFER_SIZE - conn->read_index - 1)) > 0) {
    conn->read_index += read_status;
  }

  if (read_status == 0) // client disconnect
    return err("read", "EOF received");

  if (read_status == -1) {
    if (errno == EINTR && !RUNNING) // shutdown
      NULL;
    else if (errno == EAGAIN || errno == EWOULDBLOCK) // no more data right now
      NULL;
    else
      return err("read", strerror(errno));
  }

  if (!verify_read(conn))
    return err("verify_read", NULL);

  return true;
}

bool verify_read(Connection *conn) {
  if (!conn)
    return set_efault();

  // this function will determine if the request is compelete (got the headers
  // or not), and should not be used after the headers are read
  assert(!conn->headers_found);
  assert(conn->client_buffer[conn->read_index] == '\0');

  char *headers_end = NULL;
  if (!(headers_end = strstr(conn->client_buffer, TRAILER))) {
    if (conn->read_index >= BUFFER_SIZE) { // no space left
      conn->client_status = 431;
      return err("strstr", "Headers too large");
    }
    return true; // read more
  } else
    conn->headers_found = true;

  headers_end += TRAILER_STR.len; // now past the last \n
  size_t headers_size = headers_end - conn->client_buffer;

  // tmp null termination for get_header_value(), so I do not get to the next
  // request or search for the header in the body (if read)
  char org_char = conn->client_buffer[headers_size];
  conn->client_buffer[headers_size] = '\0';

  Str misc = ERR_STR; // misc str to contain the header value
  if (get_header_value(conn->client_buffer, "Content-Length", &misc)) {
    conn->client_buffer[headers_size] = org_char;
    Str *content_len_str = &misc;

    if (content_len_str->data >
        headers_end) // this is header belongs to next request in the buf

      for (int i = 0; i < content_len_str->len; i++)
        if (!isdigit(content_len_str->data[i])) {
          conn->client_status = 400;
          return err("isdigit", "Invalid content-length header value");
        }

    // a null terminated str for atoi()
    char *temp = strndup(content_len_str->data, content_len_str->len);
    int content_len = atoi(temp);
    free(temp);

    if (!content_len) // empty body
      goto read_complete;

    if (content_len > 10 * MB) {
      conn->client_status = 413;
      return err("verify_content_len", "Content too large");
    }

    size_t request_size = headers_size + content_len;

    if (request_size == conn->read_index) // body read already, but nothing else
      goto read_complete;

    if (request_size < conn->read_index) { // body read and another request
      conn->next_index =
          request_size + 1; // will be copied to the start for next read
      goto read_complete;
    }

    conn->to_read = content_len - (conn->read_index - headers_size);
    goto disregard_body;

  } else if (get_header_value(conn->client_buffer, "Transfer-Encoding",
                              &misc)) {
    conn->client_buffer[headers_size] = org_char;
    Str *transfer_encoding = &misc;

    if (!equals(*transfer_encoding, STR("chunked"))) {
      conn->client_status = 411;
      return err("verify_encoding", "Encoding method not supported");
    }

    char *last_chunk = "0" TRAILER;

    if ((last_chunk = strstr(headers_end, last_chunk))) { // last chunk was read
      ptrdiff_t chunk_end = (last_chunk + strlen(last_chunk)) -
                            conn->client_buffer; // this is past the \n

      if (chunk_end == conn->read_index) // read the body and nothing more
        goto read_complete;

      if (chunk_end < conn->read_index) { // read the body and another request
        conn->next_index = chunk_end;
        goto read_complete;
      }

      return err("verify_last_chunk", "Logic error, last chunk index");
    }

    // last chunk not read, read more
    conn->chunked = true;
    goto disregard_body;

  } else {
    conn->client_buffer[headers_size] = org_char;
    goto read_complete;
  }

read_complete:
  conn->to_read = 0;
  return true;

disregard_body:
  conn->read_index = headers_size;
  return true;
}

bool handle_response_client(const Event *event) {
  if (!event)
    return set_efault();

  Connection *conn = event->data.ptr;

  if (!write_error_response(conn))
    return err("write_error_response", NULL);

  return true;
}

bool write_error_response(Connection *conn) {
  if (!conn)
    return set_efault();

  char date_data[DATE_LEN];
  Str date_header = {.data = date_data, .len = DATE_LEN};

  if (!set_date(&date_header))
    return err("set_date", NULL);

  if (!set_connection(conn))
    return err("set_connection", NULL);

  Str error_body_start = STR("<html>\n<head>\n<title>Error</title>\n</"
                             "head>\n<body>\n<center><h1>");

  Str error_body_end =
      STR("</h1></center>\n<hr><center>Proxy-C</center>\n</body>\n</html>");

  Str status_str = get_status_str(conn->client_status);

  ptrdiff_t body_size =
      error_body_start.len + error_body_end.len + status_str.len;

  // calculating number of bytes required to hold the final length
  // mostly will be 3, still checking
  int divisor = 1, num_of_digits = 0;
  while (body_size / divisor > 0 && ++num_of_digits)
    divisor *= 10;

  char content_len_data[num_of_digits];
  memset(content_len_data, 0, num_of_digits);

  int_to_string(body_size, content_len_data);
  if (!content_len_data[0])
    return err("int_to_string", NULL);

  Str content_length = {.data = content_len_data, .len = num_of_digits};

  const Str headers[] = {conn->http_ver,
                         SPACE,
                         status_str,
                         LINEBREAK,
                         STR("Server: "),
                         STR(SERVER),
                         LINEBREAK,
                         STR("Date: "),
                         date_header,
                         LINEBREAK,
                         STR("Content-Type: text/html"),
                         LINEBREAK,
                         STR("Content-Length: "),
                         content_length,
                         LINEBREAK,
                         STR("Connection: "),
                         conn->connection,
                         LINEBREAK,
                         STR("Location: "),
                         (Str){.data = config.upstream,
                               .len = strlen(config.upstream)},
                         TRAILER},
            body[] = {error_body_start, status_str, error_body_end, TRAILER};

  for (size_t i = 0; i < sizeof headers / sizeof(Str); i++)
    if (!write_str(conn, &headers[i]))
      return err("write_str", strerror(errno));

  for (size_t i = 0; i < sizeof body / sizeof(Str); i++)
    if (!write_str(conn, &body[i]))
      return err("write_str", strerror(errno));

  return true;
}

bool write_str(const Connection *conn, const Str *str) {
  if (!conn || !str)
    return set_efault();

  ptrdiff_t current = 0;

  while (str->len && current < str->len) {
    long wrote = write(conn->client_fd, str->data + current,
                       (size_t)(str->len - current));
    if (wrote < 0)
      return err("write", strerror(errno));
    current += wrote;
  }

  return true;
}
