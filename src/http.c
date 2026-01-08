#include <assert.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

#include "connection.h"
#include "http.h"
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
  }

  return false;
}

bool parse_head(Connection *conn, Endpoint *endpoint)
{
  // this function should not be used after the headers are read
  assert(conn && endpoint);
  assert(!endpoint->headers_found);

  // catering to both client and upstream
  // rejecting body for client, accepting body from upstream
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
      return err("check_lws", NULL);
    }

    Str key = pair.head, value = *trim_lws(&pair.tail);
    if (case_equals(key, STR("connection")))
    {
      if (endpoint->headers.connection.len)
        goto repeat_header;
      endpoint->headers.connection = value;
    }
    if (case_equals(key, STR("host")))
    {
      if (endpoint->headers.host.len)
        goto repeat_header;
      endpoint->headers.host = value;
    }
    if (case_equals(key, STR("content-length")))
    {
      if (endpoint->headers.content_length.len)
        goto repeat_header;
      endpoint->headers.content_length = value;
    }
    if (case_equals(key, STR("transfer-encoding")))
    {
      if (endpoint->headers.transfer_encoding.len)
        goto repeat_header;
      endpoint->headers.transfer_encoding = value;
    }
  }

  // should not reach here, for loop returns on empty line
  assert(false);

repeat_header:
  conn->status = client ? 400 : 500;
  return err("check_header", "Repeat header found");
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
    conn->status = 500;
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

bool parse_headers(Connection *conn, Endpoint *endpoint)
{
  assert(conn && endpoint);

  return true;
}
