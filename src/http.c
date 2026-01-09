#include <assert.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "connection.h"
#include "http.h"
#include "proxy.h"
#include "str.h"
#include "url.h"
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
    if (!pair.found || starts_with_lws(&cut.head))
    { // bad header
      conn->status = client ? 400 : 500;
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
        conn->status = client ? 400 : 500;
        return err("check_header", "Duplicate header found");
      }
      *found = value;
    }
  }

  // should not reach here, for loop returns on empty line
  assert(false);
}

bool parse_request_line(Connection *conn, Str line)
{
  assert(conn && line.len);

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
  conn->path = cut.head;

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

  conn->status = 500; // in case of error with upstream, 500 can be set before hand

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
    conn->status = client ? 400 : 500;
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
      conn->status = client ? 400 : 500;
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
    char local[headers->content_length.len + 1];
    memcpy(local, headers->content_length.data, headers->content_length.len);
    local[headers->content_length.len] = '\0';

    char *end = NULL;
    const long content_len = strtol(local, &end, 10);

    if (end == local)
    {
      conn->status = client ? 400 : 500;
      return err("strtol", "Invalid Content Length");
    }

    if (*end != '\0' || content_len < 0)
    {
      conn->status = client ? 400 : 500;
      return err("strtol", "Content Length is not a positive decimal number");
    }

    if (content_len > 10 * MB)
    {
      conn->status = client ? 413 : 500;
      return err("verify_content_len", "Content Length too large");
    }

    endpoint->content_len = (size_t)content_len;
  }

  if (headers->transfer_encoding.len)
  {
    if (!case_equals(headers->transfer_encoding, STR("chunked")))
    { // other values will likely never be used
      conn->status = client ? 400 : 500;
      return err("verify_transfer_encoding", "Invalid Transfer Encoding header");
    }
    endpoint->chunked = true;
  }

  return true;
}

bool check_body(Connection *conn, Endpoint *endpoint)
{
  assert(conn && endpoint);
  assert(!endpoint->content_len || !endpoint->chunked);

  bool client = endpoint == &conn->client, upstream = endpoint == &conn->upstream;
  assert(client || upstream);

  if (!endpoint->content_len && !endpoint->chunked)
    goto body_complete;

  if (endpoint->content_len)
  {
    if (endpoint->read_index == endpoint->head.len + (ptrdiff_t)endpoint->content_len)
      goto body_complete;

    size_t full_len = (size_t)endpoint->head.len + endpoint->content_len;
    bool extra = full_len < endpoint->read_index;

    if (extra)
    {
      endpoint->next_index = (ptrdiff_t)full_len;
      goto body_complete;
    }

    endpoint->to_read = endpoint->content_len - (size_t)(endpoint->read_index - endpoint->head.len);
  }

  if (endpoint->chunked)
    return check_last_chunk(endpoint);

  if (client) // discard body for client
    endpoint->read_index = endpoint->head.len;
  return false;

body_complete:
  endpoint->to_read = 0;
  return true;
};

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
  case 431:
    return "431 Request Header Fields Too Large";
  case 500:
    return "500 Internal Server Error";
  case 501:
    return "501 Not Implemented";
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
