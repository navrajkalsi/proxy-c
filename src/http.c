#include <assert.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

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

  for (Cut line = cut_char(tmp, '\n'); head->len += line.found ? line.head.len + 1 : 0;
       line = cut_char(tmp, '\n'))
    if (!line.found) // read more
      return false;
    else if (!line.head.len || (line.head.len == 1 && *line.head.data == '\r')) // empty line found
      return true;
    else
      tmp = line.tail;

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

  if (client && !parse_request_line(conn, endpoint))
    return err("parse_request_line", NULL);

  if (upstream && !parse_status_line(conn, endpoint))
    return err("parse_status_line", NULL);

  assert(false);

  // parse request line(client) or status line(upstream)
  return true;
}

bool parse_request_line(Connection *conn, Endpoint *client)
{
  assert(conn && client);

  Cut cut = cut_char(client->head, '\n');
  assert(cut.found); // only to be used after empty line found
  trim_cr(&cut.head);

  // request method
  cut = cut_char(cut.head, ' ');
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

bool parse_status_line(Connection *conn, Endpoint *upstream)
{
  assert(conn && upstream);

  conn->status = 500; // in case of error with upstream, 500 can be set before hand

  Cut cut = cut_char(upstream->head, '\n');
  assert(cut.found); // only to be used after empty line found
  trim_cr(&cut.head);

  // protocol
  cut = cut_char(cut.head, ' ');
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
