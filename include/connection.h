#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "http.h"
#include "main.h"
#include "utils.h"

typedef enum {
  ACCEPT_CLIENT, // only if proxy_fd is set
  READ_REQUEST,
  VERIFY_REQUEST,
  WRITE_ERROR,
  CONNECT_UPSTREAM,
  WRITE_REQUEST,
  READ_RESPONSE,
  WRITE_RESPONSE,
  CLOSE_CONN
} State;

typedef struct endpoint {
  char buffer[BUFFER_SIZE];
  int fd;
  Str buf_segment; // for client: just request headers, for upstream full
                   // response or part of it ready to be written. buffer may
                   // contain more bytes than this
  ptrdiff_t read_index;  // where to start reading again
  ptrdiff_t write_index; // where to start writing from
  size_t to_read;       // more bytes to read, incase content-length is provided
  size_t to_write;      // bytes remaining to write, across writes
  ptrdiff_t next_index; // incase 2 requests/responses arrive back to back
  bool chunked;         // transfer encoding
  bool headers_found;   // if nothing more is needed to be read from the
                        // current request, stop reading if new request is
                        // detected, in case of client
  char last_chunk_found[sizeof LAST_CHUNK]; // how much of the last chunk was
                                            // read
  HTTP http;
} Endpoint;

// struct to be used for adding/modding/deleting to the epoll instance
// every epoll_event.data in the epoll instance will have its data as a pointer
// to this struct
// target fd will depend on the state of the conn
typedef struct connection {
  struct sockaddr_storage client_addr; // filled by accept()

  Endpoint client;
  Endpoint upstream;

  int proxy_fd;

  State state;

  uint status; // http status code

  struct connection *
      *self_ptr; // this will be an element of active_conns array, used to
                 // deactive/remove from active_conns(just make this NULL)
} Connection;

// Returns a pointer to conn that needs to be added to the epoll_instance
// & activates it
Connection *init_conn(void);

void free_conn(Connection **conn);

// adds conn to the active_conns array
bool activate_conn(Connection *conn);

// removes event from active_conns array
// by making self_ptr NULL which make the array entry NULL
void deactivate_conn(Connection *conn);

// calls fcntl to set non block option on a socket
bool set_non_block(int fd);

// calls epoll_ctl with EPOLL_CTL_ADD
bool add_to_epoll(Connection *conn, int fd, int flags);

// epoll_ctl with EPOLL_CTL_MOD
bool mod_in_epoll(Connection *conn, int fd, int flags);

// epoll_ctl with EPOLL_CTL_DEL
bool del_from_epoll(int fd);
