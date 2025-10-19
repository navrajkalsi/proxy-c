#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "main.h"
#include "poll.h"
#include "request.h"
#include "utils.h"

bool handle_request(const EventData *event_data) {
  if (!event_data)
    return set_efault();

  Connection *conn = (Connection *)event_data->data.ptr;

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

  validate_request(&conn->client_request);
  return true;
}

bool validate_request(const Str *request) {
  if (!request)
    return set_efault();

  // just verifying beginning of request header
  if (request->len < (uint)sizeof "GET" ||
      memcmp(request->data, "GET ", 4) != 0)
    return err("validate_method", "Invalid method");

  // finding the host header
  char *host_ptr = strcasestr(request->data, "Host"),
       *host_end = !host_ptr ? NULL : strchr(host_ptr, '\r');

  if (!host_ptr || !host_end)
    return err("validate_host", "Host header not found");

  const Str host_header = {.data = host_ptr,
                           .len = (ptrdiff_t)(host_end - host_ptr)};

  if (!validate_host(&host_header))
    return err("validate_host", NULL);

  return true;
}
