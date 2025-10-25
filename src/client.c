#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <regex.h>
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
    if (strstr(buf, LINEBREAK.data))
      return err("verify_request", "Request headers too large");
    else
      return err("verify_request", "Bad request");
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
    err("validate_request", strerror(errno));

  print_request(conn);

  if (conn->client_status == 200)
    return true;
  else
    return false;
}

bool handle_response_client(const EventData *event_data) {
  if (!event_data)
    return set_efault();

  Connection *conn = event_data->data.ptr;

  if (conn->client_status == 200)
    // generate_response(conn);
    puts("200");
  else
    generate_error_response(conn);

  return true;
}

bool generate_response(Connection *conn) {
  if (!conn)
    return set_efault();

  return true;
}

bool generate_error_response(Connection *conn) {
  if (!conn)
    return set_efault();

  return true;
}
