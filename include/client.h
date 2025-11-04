#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "event.h"

// calls accept on listening socket fd and adds it to the epoll instance
bool accept_client(int proxy_fd);

// called after EPOLLIN is detected on a client socketclient conn
bool read_client(const Event *event);

// whether to read more and how much to read more
bool verify_read(Connection *conn);

// copies bytes from next_index to starting of buffer till read_index
// & sets read index accordingly
bool pull_buf(Connection *conn);

// searches for full last chunk from index upto null terminator
// returns true if all of last chunk is found and also sets next_index, if
// requried
bool find_last_chunk_full(Connection *conn, ptrdiff_t index);

// searches for remaining chars or starting of last chunk from index upto null
// terminator
// returns true if last_chunk is complete, else returns false also sets
// next_index, if requried
bool find_last_chunk_partial(Connection *conn, ptrdiff_t index);

bool handle_response_client(const Event *event);

// to send an error directly without contacting upstream
bool write_error_response(Connection *conn);

bool write_str(const Connection *conn, const Str *write);
