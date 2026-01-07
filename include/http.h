#include "str.h"

typedef struct connection Connection;
typedef struct endpoint Endpoint;

typedef struct headers
{
  Str connection, host, content_length, transfer_encoding;
} Headers;

// returns true if an empty line is found
// and sets the len of message to end of head
bool find_empty_line(Str *head);

// head includes: first line(request or status line, depending on the endpoint) and the headers
// return true only if headers found and ok,
// deals with error on its own
bool parse_head(Connection *conn, Endpoint *endpoint);

// returns true if all is good, return false and sets conn.status on error
bool parse_request_line(Connection *conn, Endpoint *client);

bool parse_status_line(Connection *conn, Endpoint *upstream);

bool parse_headers(Connection *conn, Endpoint *endpoint);
