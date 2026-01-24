#include <arpa/inet.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "connection.h"
#include "proxy.h"
#include "str.h"
#include "utils.h"

bool find_empty_line(Str *head)
{
  assert(head);
  Str tmp = *head;
  head->len = 0;

  if (!tmp.len)
    return false;

  for (Cut line = cut_char(tmp, '\n'); line.found; line = cut_char(tmp, '\n'))
  {
    head->len += line.head.len + 1;

    if (!line.head.len || (line.head.len == 1 && *line.head.data == '\r')) // empty line found
      return true;

    tmp = line.tail;

    if (!tmp.len)
      break;
  }

  return false;
}

bool parse_head(Connection *conn, Endpoint *endpoint)
{
  // this function should not be used after the headers are read
  assert(conn && endpoint);
  assert(!endpoint->headers_found);

  // catering to both client and upstream
  bool client = endpoint == &conn->client, upstream = endpoint == &conn->upstream;
  assert(client || upstream);

  Cut cut = cut_char(endpoint->head, '\n');
  assert(cut.found); // only use after finding empty line
  trim_cr(&cut.head);

  if (!cut.head.len)
  {
    conn->status = client ? 400 : 502;
    return err("check_first_len",
               client ? "Request Line found to be empty" : "Response Line found to be empty");
  }

  // deal with first line, depending on the endpoint type
  if (client && !parse_request_line(conn, cut.head))
    return err("parse_request_line", NULL);

  if (upstream && !parse_status_line(conn, cut.head))
    return err("parse_status_line", NULL);

  // now parse headers, endpoint agnostic
  for (cut = cut_char(cut.tail, '\n'); cut.found; cut = cut_char(cut.tail, '\n'))
  {
    trim_cr(&cut.head);
    if (!cut.head.len)
    { // parsed all
      endpoint->headers_found = true;
      return true;
    }

    Cut pair = cut_char(cut.head, ':');
    if (!pair.found || starts_with_lws(cut.head))
    { // bad header
      conn->status = client ? 400 : 502;
      return err("check_lws", "Malformed header");
    }

    Str key = pair.head, value = *trim_lws(&pair.tail), *found = NULL;

    if (case_equals(key, STR("connection")))
      found = &endpoint->headers.connection;
    if (client && case_equals(key, STR("host"))) // host is a request header
      found = &endpoint->headers.host;
    if (case_equals(key, STR("content-length")))
      found = &endpoint->headers.content_length;
    if (case_equals(key, STR("transfer-encoding")))
      found = &endpoint->headers.transfer_encoding;

    if (found)
    {
      if (found->len)
      {
        conn->status = client ? 400 : 502;
        return err("check_header", "Duplicate header found");
      }
      *found = value;
    }
  }

  // should not reach here, for loop returns on empty line
  assert(false);
  return false;
}

bool parse_request_line(Connection *conn, Str line)
{
  assert(conn);
  assert(line.len);

  // request method
  Cut cut = cut_char(line, ' ');
  if (!cut.found)
  {
    conn->status = 400;
    return err("cut_char", "Malformed Request Line");
  }
  else if (!equals(cut.head, STR("GET")))
  {
    conn->status = 405;
    return err("check_request_method", "Request method not supported");
  }

  // request target
  cut = cut_char(cut.tail, ' ');
  if (!cut.found)
  {
    conn->status = 400;
    return err("cut_char", "Malformed Request Line");
  }
  else if (!cut.head.len)
  {
    conn->status = 400;
    return err("check_request_target", "Request target empty");
  }
  else if (*cut.head.data != '/') // only supporting origin form of request targets
  {
    conn->status = 501;
    return err("check_request_target", "Request target method not supported");
  }
  conn->target = cut.head;

  if (conn->target.len > (ptrdiff_t)MAX_REQUEST_TARGET)
  {
    conn->status = 414;
    return err("check_request_target", "Request target too long");
  }

  // request protocol
  if (!equals(cut.tail, STR("HTTP/1.0")) && !equals(cut.tail, STR("HTTP/1.1")))
  {
    conn->status = 505;
    return err("check_request_protocol", "Invalid request protocol");
  }
  conn->protocol = cut.tail;

  return true;
}

bool parse_status_line(Connection *conn, Str line)
{
  assert(conn && line.len);

  conn->status = 502; // in case of error with upstream, 502 can be set before hand

  // protocol
  Cut cut = cut_char(line, ' ');
  if (!cut.found)
    return err("cut_char", "Malformed Status Line");
  else if (!equals(cut.head, conn->protocol)) // compare against request protocol
    return err("check_response_protocol", "Invalid response protocol");

  // status code
  cut = cut_char(cut.tail, ' ');
  if (!cut.found && cut.head.len != 3) // response phrase is optional and only status code is valid
    return err("cut_char", "Malformed Status Line");
  else if (!cut.head.len)
    return err("check_response_status", "Response Status Code not found");

  conn->status = 200;
  return true;
}

bool verify_headers(Connection *conn, Endpoint *endpoint)
{
  assert(conn && endpoint);

  bool client = endpoint == &conn->client, upstream = endpoint == &conn->upstream;
  assert(client || upstream);

  Headers *headers = &endpoint->headers;

  if (headers->content_length.len && headers->transfer_encoding.len)
  {
    conn->status = client ? 400 : 502;
    return err("check_body_size_type", "Both Content Length and Transfer Encoding headers found");
  }

  if (headers->connection.len)
  {
    // keepalive for client and only for upstream if keepalive from client
    if (case_equals(headers->connection, STR("keep-alive")))
      conn->keep_alive = client || conn->keep_alive ? true : false;
    // close if either wants to close
    else if (case_equals(headers->connection, STR("close")))
      conn->keep_alive = false;
    else
    {
      conn->status = client ? 400 : 502;
      return err("verify_connection", "Invalid connection header");
    }
  }
  else if (client && equals(conn->protocol,
                            STR("HTTP/1.1"))) // set default based on protocol in case of client
    conn->keep_alive = true;

  if (client) // host is only a request header
  {
    if (headers->host.len)
    {
      URL url = {0};
      if (!parse_url(headers->host, &url))
      {
        conn->status = 400;
        return err("parse_host", "Invalid host header");
      }

      if (url.protocol.len || url.path.len || url.params.len || url.frags.len)
      {
        conn->status = 400;
        return err("verify_host", "Invalid host header");
      }

      // match host to canonical
      if (!equals(url.host, config.canonical_host.host) ||
          !equals(url.port, config.canonical_host.port))
      {
        conn->status = 301;
        return err("verify_host", "Host mismatch");
      }
    }
    else if (equals(conn->protocol, STR("HTTP/1.1"))) // host header requried for http/1.1
    {
      conn->status = 400;
      return err("parse_host", "Host header missing");
    }
  }

  if (headers->content_length.len)
  {
    long content_len = -1;
    if (!str_to_long(headers->content_length, &content_len))
    {
      conn->status = client ? 400 : 502;
      return err("str_to_long", "Invalid Content Length");
    }

    if (content_len < 0)
    {
      conn->status = client ? 400 : 502;
      return err("strtol", "Content Length is not a positive decimal number");
    }

    if ((size_t)content_len > 10 * MB)
    {
      conn->status = client ? 413 : 502;
      return err("verify_content_len", "Content Length too large");
    }

    endpoint->content_len = (size_t)content_len;
  }

  if (headers->transfer_encoding.len)
  {
    if (!case_equals(headers->transfer_encoding, STR("chunked")))
    { // other values will likely never be used
      conn->status = 501;
      return err("verify_transfer_encoding", "Unknown Transfer Encoding header");
    }
    endpoint->chunked = true;
  }

  return true;
}

bool check_body(Connection *conn, Endpoint *endpoint)
{
  assert(conn && endpoint);
  assert(endpoint->headers_found);
  assert(!endpoint->content_len || !endpoint->chunked);

  bool client = endpoint == &conn->client, upstream = endpoint == &conn->upstream;
  assert(client || upstream);

  if (!endpoint->content_len && !endpoint->chunked)
    goto body_complete;

  if (endpoint->content_len)
  {
    size_t full_len = (size_t)endpoint->head.len + endpoint->content_len;

    if (endpoint->read_index == (ptrdiff_t)full_len)
      goto body_complete;

    bool extra = full_len < (size_t)endpoint->read_index;

    if (extra)
    {
      endpoint->next_index = (ptrdiff_t)full_len;
      goto body_complete;
    }

    endpoint->to_read = endpoint->content_len - (size_t)(endpoint->read_index - endpoint->head.len);
  }

  if (endpoint->chunked)
  {
    Str body = {.data = endpoint->buffer + endpoint->head.len,
                .len = endpoint->read_index - endpoint->head.len};

    if (!handle_chunked(body, conn, endpoint))
      return err("handle_chunked", NULL);

    if (endpoint->chunk_tracker.empty_found)
      goto body_complete;
  }

  return true;

body_complete:
  endpoint->to_read = 0;
  return true;
};

// limitation: does not support chunk extensions and trailers in the last chunk
bool handle_chunked(Str body, Connection *conn, Endpoint *endpoint)
{
  assert(conn && endpoint);
  assert(body.len);

  bool client = endpoint == &conn->client, upstream = endpoint == &conn->upstream;
  assert(client || upstream);

  ChunkTracker *tracker = &endpoint->chunk_tracker;

  if (tracker->state == (ChunkedState)HEAD_CRLF)
  {
    if (tracker->view_len && tracker->buffer[tracker->view_len - 1] == '\r')
    { // just missing lf
      if (tracker->view_len >= (ptrdiff_t)MAX_CHUNK_HEAD - 1)
      {
        conn->status = client ? 501 : 502;
        return err("verify_head_len", "Head of the chunk too long");
      }

      if (*body.data != '\n') // first char should be lf
      {
        conn->status = client ? 400 : 502;
        return err("verify_head_crlf", "Malformed CRLF");
      }

      tracker->buffer[tracker->view_len++] = '\n';

      if (!extract_chunk_size((Str){tracker->buffer, tracker->view_len}, conn, endpoint))
        return err("extract_chunk_size", NULL);

      tracker->view_len = 0;
      body.data++;
      if (!--body.len) // read more from endpoint
        return true;

      return handle_chunked(body, conn, endpoint);
    }

    if (tracker->view_len)
    { // try to find crlf, if not add to buffer if the size allows
      Cut c = cut_str(body, CRLF);
      if (!c.found && tracker->view_len + body.len + CRLF.len > (ptrdiff_t)MAX_CHUNK_HEAD)
      {
        conn->status = client ? 501 : 502;
        return err("verify_head_len", "Head of the chunk too long");
      }

      // copy to buffer, in case continuing to read, or extracting chunk len
      ptrdiff_t copy = c.found ? c.head.len + CRLF.len : body.len;
      memcpy(tracker->buffer + tracker->view_len, body.data, (size_t)copy);
      tracker->view_len += copy;

      if (!c.found) // continue reading chunk header
        return true;

      if (!extract_chunk_size((Str){tracker->buffer, tracker->view_len}, conn, endpoint))
        return err("extract_chunk_size", NULL);

      tracker->view_len = 0;
      body.data += copy;
      if (!(body.len -= copy)) // read more
        return true;

      return handle_chunked(body, conn, endpoint);
    }

    // reading fresh
    // could be merged with the previous if, but keeping separate for clarity
    Cut c = cut_str(body, CRLF);
    if (!c.found && body.len + CRLF.len > (ptrdiff_t)MAX_CHUNK_HEAD)
    {
      conn->status = client ? 501 : 502;
      return err("verify_head_len", "Head of the chunk too long");
    }

    // copy to buffer, in case continuing to read, or extracting chunk len
    ptrdiff_t copy = c.found ? c.head.len + CRLF.len : body.len;
    memcpy(tracker->buffer + tracker->view_len, body.data, (size_t)copy);
    tracker->view_len += copy;

    if (!c.found) // continue reading chunk header
      return true;

    if (!extract_chunk_size((Str){tracker->buffer, tracker->view_len}, conn, endpoint))
      return err("extract_chunk_size", NULL);

    tracker->view_len = 0;
    body.data += copy;
    if (!(body.len -= copy)) // read more from endpoint
      return true;

    return handle_chunked(body, conn, endpoint);
  }

  else if (tracker->state == (ChunkedState)CHUNK)
  {
    if ((size_t)body.len < tracker->chunk_len - tracker->bytes_read)
    { // increment bytes_read and read more
      tracker->bytes_read += (size_t)body.len;
      return true;
    }

    // no need to update bytes_read, causing moving on to next state
    tracker->state = TAIL_CRLF;
    body.data += tracker->chunk_len - tracker->bytes_read;
    if (!(body.len -=
          (ptrdiff_t)(tracker->chunk_len - tracker->bytes_read))) // read more from endpoint
      return true;

    return handle_chunked(body, conn, endpoint);
  }

  else if (tracker->state == (ChunkedState)TAIL_CRLF)
  { // limitation: does not handle trailers in tail crlf
    assert(tracker->view_len <= 1);
    if (tracker->view_len == 1)
    {
      assert(*tracker->buffer == '\r');
      if (*body.data != '\n')
      {
        conn->status = client ? 400 : 502;
        return err("verify_tail_crlf", "Malformed CRLF");
      }

      if (!tracker->chunk_len && !tracker->bytes_read)
      { // done
        tracker->empty_found = true;
        if (body.len > 1) // next request to read
          endpoint->next_index = ++body.data - endpoint->buffer;
        return true;
      }

      tracker->view_len = 0;
      tracker->state = HEAD_CRLF;
      body.data++;
      if (!--body.len) // read more from endpoint
        return true;

      return handle_chunked(body, conn, endpoint);
    }

    if (*body.data != '\r')
    {
      conn->status = client ? 400 : 502;
      return err("verify_tail_crlf", "Malformed CRLF");
    }

    if (body.len == 1)
    { // only read \r
      *tracker->buffer = '\r';
      tracker->view_len = 1;
      return true;
    }

    if (body.data[1] != '\n')
    {
      conn->status = client ? 400 : 502;
      return err("verify_tail_crlf", "Malformed CRLF");
    }

    if (!tracker->chunk_len && !tracker->bytes_read)
    { // done
      tracker->empty_found = true;
      if (body.len > CRLF.len) // next request to read
        endpoint->next_index = body.data + CRLF.len - endpoint->buffer;
      return true;
    }

    reset_chunk_tracker(tracker);

    body.data += CRLF.len;
    if (!(body.len -= CRLF.len)) // read more from endpoint
      return true;

    return handle_chunked(body, conn, endpoint);
  }

  assert(false);
  return false;
}

void reset_chunk_tracker(ChunkTracker *tracker)
{
  assert(tracker);

  tracker->state = HEAD_CRLF;
  tracker->view_len = 0;
  tracker->chunk_len = 0;
  tracker->bytes_read = 0;
  tracker->empty_found = false;
}

bool extract_chunk_size(Str head, Connection *conn, Endpoint *endpoint)
{
  assert(conn && endpoint);
  assert(head.len);

  bool client = endpoint == &conn->client, upstream = endpoint == &conn->upstream;
  assert(client || upstream);

  ChunkTracker *tracker = &endpoint->chunk_tracker;
  assert(tracker->state == (ChunkedState)HEAD_CRLF);

  ptrdiff_t len = -1;
  assert((len = contains(head, STR("\r\n"))) != -1);
  head.len = len;

  if (contains(head, STR(";")) != -1)
  {
    conn->status = client ? 501 : 502;
    return err("contains", "Extensions detected in chunk head");
  }

  long size = -1;
  if (!str_to_long_hex(head, &size))
  {
    conn->status = client ? 400 : 502;
    return err("str_to_long_hex", NULL);
  }

  // ready to read chunk
  tracker->chunk_len = (size_t)size;
  tracker->bytes_read = 0;
  tracker->state = CHUNK;

  return true;
};

void print_request(const Connection *conn)
{
  assert(conn);
  const Endpoint *client = &conn->client;
  assert(client && client->headers_found && client->head.len);

  char ip_str[INET6_ADDRSTRLEN];
  if (!inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)&conn->client_addr)->sin6_addr), ip_str,
                 sizeof ip_str))
    err("inet_ntop", strerror(errno));
  else
    printf("\n(%s) ", ip_str);

  for (ptrdiff_t i = 0;
       i < client->head.len && client->head.data[i] != '\r' && client->head.data[i] != '\n'; i++)
    putchar(client->head.data[i]);

  putchar(' ');

  // host
  put_str(client->headers.host);
  putchar('\n');
}

char *get_status_string(uint status)
{
  // these strings live for the entire life of the program
  switch (status)
  {
  case 200:
    return "200 OK";
  case 301:
    return "301 Moved Permanently";
  case 400:
    return "400 Bad Request";
  case 403:
    return "403 Forbidden";
  case 404:
    return "404 Not Found";
  case 405:
    return "405 Method Not Allowed";
  case 408:
    return "408 Request Timout";
  case 413:
    return "413 Content Too Large";
  case 414:
    return "414 URI Too Long";
  case 431:
    return "431 Request Header Fields Too Large";
  case 500:
    return "500 Internal Server Error";
  case 501:
    return "501 Not Implemented";
  case 502:
    return "502 Bad Gateway";
  case 504:
    return "504 Gateway Timeout";
  case 505:
    return "505 HTTP Version Not Supported";
  }

  assert(false);
}

Str get_status_str(uint status)
{
  return WRAP_STR(get_status_string(status));
}
