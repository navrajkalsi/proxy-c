#include "str.h"
#include <sys/types.h>

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
// return true only if headers found
// in case of error sets status for writing error
bool parse_head(Connection *conn, Endpoint *endpoint);

// returns true if all is good, return false and sets conn.status on error
bool parse_request_line(Connection *conn, Str line);

bool parse_status_line(Connection *conn, Str line);

bool verify_headers(Connection *conn, Endpoint *endpoint);

// returns true if body has been read
// determines size of body by content len or transfer chunked
bool check_body(Connection *conn, Endpoint *endpoint);

char *get_status_string(uint status);

Str get_status_str(uint status);
