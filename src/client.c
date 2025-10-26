#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <regex.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "client.h"
#include "http.h"
#include "main.h"
#include "poll.h"
#include "utils.h"

bool accept_client(int proxy_fd, int epoll_fd) {
  // looping, as there epoll might be waken up by multiple incoming requests on
  // this fd
  while (RUNNING) {
    Connection *conn = NULL;
    EventData *data = NULL;

    if (!(conn = init_connection()))
      return err("init_connection", NULL);

    if (!(data =
              init_event_data(TYPE_PTR_CLIENT, (epoll_data_t)(void *)conn))) {
      free_connection(&conn);
      return err("init_event_data", NULL);
    }
    socklen_t addr_len = sizeof conn->client_addr;

    if ((conn->client_fd =
             accept(proxy_fd, (struct sockaddr *)&conn->client_addr,
                    &addr_len)) == -1) {
      if (errno == EINTR && !RUNNING) // shutdown
        break;

      if (errno == EAGAIN || errno == EWOULDBLOCK) // no more connections / data
        break;

      if (errno == ECONNABORTED)
        // client aborted
        continue;

      free_event_conn(&data);
      return err("accept", strerror(errno));
    }

    if (!set_non_block(conn->client_fd)) {
      free_event_conn(&data);
      return err("set_non_block", strerror(errno));
    }

    if (!add_to_epoll(epoll_fd, data,
                      EPOLLIN | EPOLLHUP | EPOLLET | EPOLLERR | EPOLLRDHUP |
                          EPOLLONESHOT)) {
      free_event_conn(&data);
      return err("add_to_epoll", strerror(errno));
    }
  }

  return true;
}

bool handle_request_client(const EventData *event_data) {
  if (!event_data)
    return set_efault();

  Connection *conn = event_data->data.ptr;

  // event_data SHOULD ALWAYS contain the data as a pointer to a conn
  assert(event_data->data_type == TYPE_PTR_CLIENT);
  assert(conn);

  char *buf = conn->client_buffer, *buf_ptr = buf, *end_ptr = NULL;
  size_t total_read = 0;
  ssize_t read_status = 0;

  // Only headers are considered, cause I currently support GET only,
  // anything after TRAILER is disregarded
  while ((read_status = read(conn->client_fd, buf_ptr,
                             BUFFER_SIZE - total_read - 1)) > 0) {
    total_read += (size_t)read_status;
    buf_ptr = buf + total_read;
    buf[total_read] = '\0';

    if ((end_ptr = strstr(buf, TRAILER.data)))
      // No need to read more
      break;
  }

  // if there is nothing to read, client closed
  if (buf == buf_ptr &&
      !total_read) // I always receive an empty request in the beginning, as
                   // the browser connects and immediately disconnects
    return err("verify_request",
               "empty request"); // this is not a bug, just how TCP works

  if (read_status != -1 && end_ptr) { // everything is alright, got the headers
    // Advancing the ptr by 4 chars to get past the request end
    // Then comparing with buf_ptr to see if they are same
    // If same that means there is no body after headers and the total_read
    // is the correct length, else change total_read to the length of only
    // the request headers
    end_ptr += TRAILER.len;
    *end_ptr = '\0'; // null terminating for strcasestr() use later
    if (end_ptr != buf_ptr)
      // Discard if there is any body in the request
      // Only supporting GET requests for now
      // so don't need any body
      total_read = (size_t)(end_ptr - buf);
  } else if (read_status != -1 && !end_ptr) { // could not find end of headers
    // first looking if i got the first request line, if not the request is
    // just not a valid request probably (bad request) looking for a
    // linebreak
    // "\r\n" for request line
    if (strstr(buf, LINEBREAK.data)) {
      conn->client_status = 431;
      return err("verify_request", "Request headers too large");
    } else {
      conn->client_status = 400;
      return err("verify_request", "Bad request");
    }
  } else
    return err("read", strerror(errno));

  // At this point total_read is the correct len of data in buf
  // request can only be used in this scope!!
  conn->client_request.data = buf;
  conn->client_request.len = (ptrdiff_t)total_read;

  // Handle request sets the required response codes
  // Only parsing (handle_request) if the request line is present
  // while (equals(&client->connection, &STR("keep-alive")));

  if (!validate_request(conn))
    err("validate_request", NULL);

  print_request(conn);

  if (conn->client_status == 200)
    return true;
  else {
    write_error_response(conn);
    return false;
  }
}

bool handle_response_client(const EventData *event_data) {
  if (!event_data)
    return set_efault();

  Connection *conn = event_data->data.ptr;

  if (conn->client_status == 200)
    puts("200");
  else
    write_error_response(conn);

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

  Str error_body_start = STR(
      "<html>\n<head>\n<title>Error</title>\n</head>\n<body>\n<center><h1>");

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
