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
#include "event.h"
#include "http.h"
#include "main.h"
#include "utils.h"

void accept_client(int proxy_fd) {
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

    if (!add_to_epoll(event, READ_FLAGS)) {
      free_event_conn(&event);
      err("add_to_epoll", NULL);
      continue;
    }
    // now the new client will be accepted to make a request
  }

  return;
}

void read_client(const Event *event) {
  if (!event)
    goto error;

  Connection *conn = event->data.ptr;

  // event SHOULD ALWAYS contain the data as a pointer to a conn
  assert(event->data_type == TYPE_PTR_CLIENT);
  assert(conn && conn->state == READ_REQUEST);

  // in case continuing to read after dealing with previous request
  if (conn->next_index)
    if (!pull_buf(conn)) {
      err("pull_buf", strerror(errno));
      goto error;
    }

  ssize_t read_status = 0;

  // new request should always start from the beginning of the buffer
  while (conn->to_read &&
         (read_status =
              read(conn->client_fd, conn->client_buffer + conn->read_index,
                   conn->to_read)) > 0) {

    conn->client_buffer[conn->read_index + read_status] = '\0';
    puts(conn->client_buffer);

    // if chunked then reject the body
    // else read upto to_read (which is maybe set by content len if headers were
    // found)
    conn->to_read = conn->chunked ? (size_t)(BUFFER_SIZE - conn->read_index - 1)
                                  : conn->to_read - read_status;

    if (!conn->headers_found) {
      conn->read_index += read_status;
      if (!verify_read(
              conn)) { // error response status set, send error response
        conn->state = WRITE_ERROR;
        break;
      }

      // no content len or encoding was specified
      if (conn->headers_found && !conn->to_read &&
          !conn->chunked) { // request complete, now verify it
        conn->state = VERIFY_REQUEST;
        break;
      }
      continue;
    }

    if (conn->chunked) { // checking for last chunk, was not received during
                         // verify_read()

      // remember, here read_index will be the index of headers_end (one byte
      // after last \n)
      // AND read_index has NOT been advanced by read_status

      if (find_last_chunk(conn)) {
        conn->state = VERIFY_REQUEST;
        break;
      }
    }

    if (!conn->to_read) // done reading body, set from content-len
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
  conn->client_status = 500;
  return;
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
    if (conn->read_index >= BUFFER_SIZE - 1) { // no space left
      conn->client_status = 431;
      return err("strstr", "Headers too large");
    }
    return true; // read more
  } else
    conn->headers_found = true;

  headers_end += TRAILER_STR.len; // now past the last \n
  size_t headers_size = headers_end - conn->client_buffer;

  conn->client_headers.data = conn->client_buffer;
  conn->client_headers.len = headers_size;

  // tmp null termination for get_header_value(), so I do not get to the next
  // request or search for the header in the body (if read)
  char org_char = conn->client_buffer[headers_size];
  conn->client_buffer[headers_size] = '\0';

  Str misc = ERR_STR; // misc str to contain the header value
  if (get_header_value(conn->client_buffer, "Content-Length", &misc)) {
    conn->client_buffer[headers_size] = org_char;
    Str *content_len_str = &misc;

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

    if (request_size ==
        (size_t)conn->read_index) // body read already, but nothing else
      goto read_complete;

    if (request_size <
        (size_t)conn->read_index) { // body read and another request
      conn->next_index =
          request_size + 1; // will be copied to the start for next read
      goto read_complete;
    }

    conn->to_read = request_size - conn->read_index;
    goto disregard_body;

  } else if (get_header_value(conn->client_buffer, "Transfer-Encoding",
                              &misc)) {
    conn->client_buffer[headers_size] = org_char;
    Str *transfer_encoding = &misc;

    if (!equals(*transfer_encoding, STR("chunked"))) {
      conn->client_status = 411;
      return err("verify_encoding", "Encoding method not supported");
    }

    // finding full last chunk or partial from last few bytes
    // worst case, got: '0\r\n\r'
    if (find_last_chunk(conn))
      goto read_complete;

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

bool pull_buf(Connection *conn) {
  if (!conn)
    return set_efault();

  if (!conn->next_index) // nothing to do, read new request
    return true;

  assert(conn->read_index > conn->next_index);

  size_t to_copy = conn->read_index - conn->next_index;
  memcpy(conn->client_buffer, conn->client_buffer + conn->next_index, to_copy);
  conn->read_index = to_copy;
  conn->to_read = BUFFER_SIZE - conn->read_index - 1;
  conn->next_index = 0;

  conn->headers_found = false;

  return true;
}

bool find_last_chunk(Connection *conn) {
  if (!conn)
    return set_efault();

  assert(conn->headers_found);

  // pointer at chars after headers
  char *start = conn->client_buffer + conn->client_headers.len;

  if (*start == '\0') // no body
    return false;

  // first checking if already reading last chunk from previous call
  size_t matched = strlen(conn->last_chunk_found);

  while (matched && matched < (size_t)LAST_CHUNK_STR.len &&
         *start) {                       // continue to check for last_chunk
    if (*start != LAST_CHUNK[matched]) { // mismatch
      *conn->last_chunk_found = '\0';
      matched = 0;
      break;
    }

    conn->last_chunk_found[matched] = LAST_CHUNK[matched];
    conn->last_chunk_found[++matched] = '\0';

    start++;
  }

  if (matched == (size_t)LAST_CHUNK_STR.len) { // full chunk matched
    if (*start)                                // next request
      conn->next_index = start - conn->client_buffer;
    return true;
  } else if (matched) // full chunk not matched but ran out of chars
    return false;     // read more

  // nothing more to compare
  if (!*start)
    return false;

  // last chunk not found in the beginning
  char *last_chunk = LAST_CHUNK;
  if ((last_chunk = strstr(start, last_chunk))) { // last chunk was read
    ptrdiff_t chunk_end = (last_chunk + LAST_CHUNK_STR.len) -
                          conn->client_buffer; // this is past the \n

    if (conn->client_buffer[chunk_end]) // read the body and another request
      conn->next_index = chunk_end;

    // read the body and nothing more
    return true;
  }

  // full last chunk not found, check last bytes in the buffer for worst case:
  // '0\r\n\r'
  start = conn->client_buffer + conn->client_headers.len;
  size_t read_size = strlen(start), // num of chars available to check at most
      to_match = read_size < (size_t)LAST_CHUNK_STR.len - 1
                     ? read_size
                     : (size_t)LAST_CHUNK_STR.len - 1;
  ptrdiff_t match_index = read_size - to_match;
  matched = 0;

  // checking if 0 is received, only using last to_match bytes
  while (start[match_index]) {
    if (start[match_index] != LAST_CHUNK[matched]) {
      matched = 0;
      *conn->last_chunk_found = '\0'; // restart
    } else {
      conn->last_chunk_found[matched] = LAST_CHUNK[matched];
      conn->last_chunk_found[++matched] = '\0';
    }

    match_index++;
  }

  return false;
}

bool find_last_chunk_full(Connection *conn, ptrdiff_t index) {
  if (!conn)
    return set_efault();

  char *last_chunk = LAST_CHUNK;

  if ((last_chunk = strstr(conn->client_buffer + index,
                           last_chunk))) { // last chunk was read
    ptrdiff_t chunk_end = (last_chunk + LAST_CHUNK_STR.len) -
                          conn->client_buffer; // this is past the \n

    if (conn->client_buffer[chunk_end] !=
        '\0') // read the body and another request
      conn->next_index = chunk_end;

    // read the body and nothing more
    return true;
  }

  return false;
}

bool find_last_chunk_partial(Connection *conn, ptrdiff_t index) {
  if (!conn)
    return set_efault();

  // checking if 0 is received, only using last few bytes
  // extreme case, received: "0\r\n\r"
  while (conn->client_buffer[index] != '\0') {
    size_t found =
        strlen(conn->last_chunk_found); // chars already found for last chunk,
                                        // will be zero in case starting new
                                        // can change in this loop

    printf("found: %zu\n", found);
    if (found == (size_t)LAST_CHUNK_STR.len)
      break;

    if (conn->client_buffer[index] != LAST_CHUNK[found])
      *conn->last_chunk_found = '\0'; // restart
    else {
      conn->last_chunk_found[found] = LAST_CHUNK[found];
      conn->last_chunk_found[++found] = '\0';
    }

    index++;
  }

  if (strlen(conn->last_chunk_found) == (size_t)LAST_CHUNK_STR.len) {
    if (conn->client_buffer[index] != '\0')
      conn->next_index = index;
    return true;
  } else
    return false;
}

bool verify_request(Connection *conn) {
  if (!conn)
    return set_efault();
}

bool handle_error_response(Connection *conn) {
  if (!conn)
    return set_efault();

  assert(conn->client_status && conn->client_status >= 300);

  // using upstream buffer as client buffer may contain another request, but
  // upstream buffer will be empty
  if (!generate_error_response(conn))
    return err("generate_error_response", NULL);

  if (!write_error_response(conn))
    return err("write_error_response", NULL);

  return true;
}

bool generate_error_response(Connection *conn) {
  if (!conn)
    return set_efault();

  assert(conn->client_status >= 300);

  char date[DATE_LEN] = {0};
  if (!set_date_string(date))
    return err("set_date_string", NULL);

  const Str err_str = get_status_str(conn->client_status),
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
            location = {.data = config.upstream,
                        .len = (ptrdiff_t)strlen(config.upstream)},
            response_headers[] = {
                STR("HTTP/1.1 "),
                err_str,
                STR("\r\nServer: " SERVER "\r\nDate: "),
                date_str,
                STR("\r\nContent-Type: text/html\r\nContent-Length: "),
                content_length,
                STR("\r\nConnection: "),
                conn->client_status >= 400
                    ? STR("keep-alive")
                    : STR("close"), // close for redirections
                conn->client_status < 400
                    ? STR("\r\nLocation: ")
                    : ERR_STR, // location only for redirections
                conn->client_status < 400 ? location : ERR_STR,
                STR("\r\n\r\n")};

  // collecting all response in upstream_buffer
  size_t header_elms = sizeof response_headers / sizeof(Str), headers_size = 0;
  for (uint i = 0; i < header_elms; ++i)
    headers_size += response_headers[i].len;

  if (headers_size + body_size > BUFFER_SIZE)
    return err("collect_response", "Error response too big");

  ptrdiff_t buf_ptr = 0;

  for (uint i = 0; i < header_elms; ++i) {
    memcpy(conn->upstream_buffer + buf_ptr, response_headers[i].data,
           response_headers[i].len);
    buf_ptr += response_headers[i].len;
  }
  for (uint i = 0; i < body_elms; ++i) {
    memcpy(conn->upstream_buffer + buf_ptr, response_body[i].data,
           response_body[i].len);
    buf_ptr += response_body[i].len;
  }
  conn->upstream_buffer[buf_ptr] = '\0';
  conn->to_write = (size_t)buf_ptr;

  return true;
}

bool write_error_response(Connection *conn) {
  if (!conn)
    return set_efault();

  ssize_t write_status = 0;

  while ((conn->to_write -= write_status) &&
         (write_status =
              write(conn->client_fd, conn->upstream_buffer + conn->write_index,
                    conn->to_write)) > 0)
    conn->write_index += write_status;

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

bool write_error_response1(Connection *conn) {
  if (!conn)
    return set_efault();

  char date_data[DATE_LEN];
  Str date_header = {.data = date_data, .len = DATE_LEN};

  if (!set_date_str(&date_header))
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
                         SPACE_STR,
                         status_str,
                         LINEBREAK_STR,
                         STR("Server: "),
                         STR(SERVER),
                         LINEBREAK_STR,
                         STR("Date: "),
                         date_header,
                         LINEBREAK_STR,
                         STR("Content-Type: text/html"),
                         LINEBREAK_STR,
                         STR("Content-Length: "),
                         content_length,
                         LINEBREAK_STR,
                         STR("Connection: "),
                         conn->connection,
                         LINEBREAK_STR,
                         STR("Location: "),
                         (Str){.data = config.upstream,
                               .len = strlen(config.upstream)},
                         TRAILER_STR},
            body[] = {error_body_start, status_str, error_body_end,
                      TRAILER_STR};

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
