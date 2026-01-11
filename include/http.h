#include <sys/types.h>

#include "main.h"
#include "str.h"

typedef struct connection Connection;
typedef struct endpoint Endpoint;

typedef enum chunked_state
{            // what part of a chunk are we reading
  START,     // read first chunk
  SIZE_CRLF, // hex encoded size
  CHUNK,     // actual chunk bytes
  END_CRLF   // ending delimiter
} ChunkedState;

typedef struct chunk_tracker
{
  char chunk_buffer[CHUNKED_BUFFER_SIZE]; // max len (64 bit int) 16 bytes in hex and 2 for crlf
  Str chunk_buffer_str;                   // str helper for buffer
  ChunkedState state;
  size_t chunk_len;
  size_t bytes_read;
  bool empty_found;
} ChunkTracker;

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
// only to be used once per request, right after parsing verifying headers
bool check_body(Connection *conn, Endpoint *endpoint);

void reset_chunk_tracker(ChunkTracker *tracker);

// returns false on error, sets emtpy_found in tracker to indicate that the body is compelete
// extra is set to be used for next index
bool check_empty_chunk(Str body, ChunkTracker *tracker, size_t *extra);

char *get_status_string(uint status);

Str get_status_str(uint status);
