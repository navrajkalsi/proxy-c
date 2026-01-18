#pragma once

#include <openssl/err.h>
#include <sys/socket.h>

#include "http.h"

typedef enum
{
  ACCEPT_CLIENT, // only if PROXY_FD is set
  PEEK_CLIENT,   // to determine the protocol used by client
  TLS_CLIENT,
  READ_REQUEST,
  WRITE_ERROR,
  CONNECT_UPSTREAM,
  TLS_UPSTREAM,
  WRITE_REQUEST,
  READ_RESPONSE,
  WRITE_RESPONSE,
  CHECK_CONN,
  SSL_READ,  // if ssl errors with SSL_ERROR_WANT_READ
  SSL_WRITE, // if ssl errors with SSL_ERROR_WANT_WRITE
  CONN_TIMEDOUT,
  STATE_TIMEDOUT,
  CLOSE_CONN
} State;

typedef enum
{
  CLIENT,
  UPSTREAM
} EndpointType;

typedef struct endpoint
{
  char buffer[BUFFER_SIZE];
  Headers headers;
  ChunkTracker chunk_tracker; // struct for tracking last chunk incase of chunked encoding
  Str head;                   // start of request to empty line
  SSL *ssl;
  ptrdiff_t read_index;  // where to start reading again
  ptrdiff_t write_index; // where to start writing from
  ptrdiff_t next_index;  // incase 2 or more requests/responses arrive back to back
  size_t to_read;        // more bytes to read
  size_t to_write;       // bytes remaining to write, across writes
  size_t content_len;    // for client - len of req body,upstream - len of res body
  int fd;
  EndpointType type;
  bool chunked;       // transfer encoding
  bool headers_found; // if nothing more is needed to be read from the current request,
                      // stop reading if new request is detected, in case of client
} Endpoint;

// struct to be used for adding/modding/deleting to the epoll instance
// every epoll_event.data in the epoll instance will have its data as a pointer to this struct
// target fd will depend on the state of the conn
typedef struct connection
{
  struct sockaddr_storage client_addr; // filled by accept()
  Endpoint client;
  Endpoint upstream;
  Str target;
  Str protocol;
  struct connection **self_ptr; // this will be an element of active_conns array, used to
                                // deactive/remove from active_conns(just make this NULL)
  State state;
  State timeout_state; // state conn was in right before timing out
  int conn_tfd;        // full conn timeout, also use for keep-alive
  int state_tfd;       // timeout for individual read/write states
  uint status;         // http status code
  bool complete;       // full response received and sent
  bool keep_alive;
} Connection;

// global array of conn structs that were added to the epoll table
// init & free conn() add and remove from this array automatically
extern Connection *active_conns[MAX_CONNECTIONS];
extern int active_conns_num; // for future use, should not be used as index for active_conns array

// Returns a pointer to conn that needs to be added to the epoll_instance & activates it
Connection *init_conn(void);

// inits conn struct without timerfds, only for use in for proxy(listening) fd
Connection *init_proxy_conn(void);

void free_conn(Connection **conn);

// adds conn to the active_conns array and starts its timeout
bool activate_conn(Connection *conn);

// removes event from active_conns array by making self_ptr NULL which make the array entry NULL
// also removes all conn timeouts
void deactivate_conn(Connection *conn);

// resets connection variables to their defaults to start a new request
void reset_conn(Connection *conn);

// calls fcntl to set non block option on a socket
void set_non_block(int fd);

// calls epoll_ctl with EPOLL_CTL_ADD
void add_to_epoll(Connection *conn, int fd, int flags);

// epoll_ctl with EPOLL_CTL_MOD
void mod_in_epoll(Connection *conn, int fd, int flags);

// epoll_ctl with EPOLL_CTL_DEL
void del_from_epoll(int fd);

// copies bytes from next_index to starting of buffer till read_index & sets read index accordingly
void pull_buf(Endpoint *endpoint);

// used to continue the conn, if keep alive is true
void check_conn(Connection *conn);

// for debugging
void print_endpoint(const Endpoint *endpoint);

// setups ssl object for the specific endpoint
// DOES NOT verify, if the config option is set to true or not
// verify before calling
// adapts ssl_accept or ssl_connect calls depending on upstream or client
bool setup_endpoint_tls(Connection *conn, Endpoint *endpoint);

// uses clients buffer to piece together the redirect
Str get_redirect_location(Connection *conn);
