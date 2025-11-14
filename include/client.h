#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "connection.h"

// calls accept on listening socket fd and adds it to the epoll instance
void accept_client(int proxy_fd);

// called after EPOLLIN is detected on a client socketclient conn
void read_client(Connection *conn);

// whether to read more and how much to read more
bool verify_read(Connection *conn);

// copies bytes from next_index to starting of buffer till read_index
// & sets read index accordingly
bool pull_buf(Connection *conn);

// dynamically checks for last_chunk (fragmented or full) depending on the
// chars in conn.last_chunk_found
// starts to check from end of headers in client_buffer
// returns true if chunk is received in full, or false if need to read more
bool find_last_chunk(Connection *conn);

// searches for full last chunk from index upto null terminator
// returns true if all of last chunk is found and also sets next_index, if
// requried
bool find_last_chunk_full(Connection *conn, ptrdiff_t index);

// searches for remaining chars or starting of last chunk from index upto null
// terminator
// returns true if last_chunk is complete, else returns false also sets
// next_index, if requried
bool find_last_chunk_partial(Connection *conn, ptrdiff_t index);

// Proxy side request verification
// host header verification
bool verify_request(Connection *conn);

// sending the error status code to client, in case of error during read()
bool handle_error_response(Connection *conn);

bool generate_error_response(Connection *conn);

// to send an error directly without contacting upstream
bool write_error_response(Connection *conn);

bool write_str(const Connection *conn, const Str *write);
