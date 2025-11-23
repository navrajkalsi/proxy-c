#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "main.h"
#include "timeout.h"
#include "utils.h"

typedef enum
{
  ACCEPT_CLIENT, // only if proxy_fd is set
  READ_REQUEST,
  VERIFY_REQUEST,
  WRITE_ERROR,
  CONNECT_UPSTREAM,
  WRITE_REQUEST,
  READ_RESPONSE,
  WRITE_RESPONSE,
  CHECK_CONN,
  CLOSE_CONN
} State;

typedef struct endpoint
{
  char buffer[BUFFER_SIZE];
  int fd;
  Str headers;           // buffer may contain more bytes than this
  ptrdiff_t read_index;  // where to start reading again
  ptrdiff_t write_index; // where to start writing from
  size_t to_read;        // more bytes to read, incase content-length is provided
  size_t to_write;       // bytes remaining to write, across writes
  ptrdiff_t next_index;  // incase 2 requests/responses arrive back to back
  size_t content_len;    // for client - len of req body,upstream - len of res body
  bool chunked;          // transfer encoding
  bool headers_found;    // if nothing more is needed to be read from the current request,
                         // stop reading if new request is detected, in case of client
  char last_chunk_found[sizeof LAST_CHUNK]; // how much of the last chunk was read
} Endpoint;

// struct to be used for adding/modding/deleting to the epoll instance
// every epoll_event.data in the epoll instance will have its data as a pointer to this struct
// target fd will depend on the state of the conn
typedef struct connection
{
  struct sockaddr_storage client_addr; // filled by accept()

  Endpoint client;
  Endpoint upstream;

  Str http_ver;
  Str path;
  Str host;

  int proxy_fd;

  State state;

  uint status;   // http status code
  bool complete; // full response received and sent
  bool keep_alive;

  struct connection **self_ptr;    // this will be an element of active_conns array, used to
                                   // deactive/remove from active_conns(just make this NULL)
  Timeout *timeouts[TIMEOUTTYPES]; // each conn can have at most TIMEOUTTYPES of timeouts
                                   // associated with it at once, one for each state
} Connection;

// global array of conn structs that were added to the epoll table
// init & free conn() add and remove from this array automatically
extern Connection *active_conns[MAX_CONNECTIONS];

// Returns a pointer to conn that needs to be added to the epoll_instance & activates it
Connection *init_conn(void);

void free_conn(Connection **conn);

// adds conn to the active_conns array
bool activate_conn(Connection *conn);

// removes event from active_conns array by making self_ptr NULL which make the array entry NULL
void deactivate_conn(Connection *conn);

// resets connection variables to their defaults to start a new request
void reset_conn(Connection *conn);

// calls fcntl to set non block option on a socket
bool set_non_block(int fd);

// calls epoll_ctl with EPOLL_CTL_ADD
bool add_to_epoll(Connection *conn, int fd, int flags);

// epoll_ctl with EPOLL_CTL_MOD
bool mod_in_epoll(Connection *conn, int fd, int flags);

// epoll_ctl with EPOLL_CTL_DEL
bool del_from_epoll(int fd);

// copies bytes from next_index to starting of buffer till read_index & sets read index accordingly
void pull_buf(Endpoint *endpoint);

// dynamically checks for last_chunk (fragmented or full) depending on the
// chars in endpoint.last_chunk_found
// starts to check from end of headers in client_buffer
// returns true if chunk is received in full, or false if need to read more
bool find_last_chunk(Endpoint *endpoint);

// parsing common headers for both client and upstream only call once per request/response
bool parse_headers(Connection *conn, Endpoint *endpoint);

// used to continue the conn, if keep alive is true
void check_conn(Connection *conn);
