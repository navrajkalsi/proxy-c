#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "main.h"
#include "utils.h"

// if connection is storing a fd (only in case of listening sock) or ptr
typedef enum { TYPE_FD, TYPE_PTR_CLIENT, TYPE_PTR_UPSTREAM } DataType;

typedef enum {
  READ_REQUEST,
  WRITE_RESPONSE,
  READ_RESPONSE,
  WRITE_REQUEST
} State;

// struct to be used for adding/modding/deleting to the epoll instance
// every epoll_event in the epoll instance will have its data as a pointer to
// this struct
// this structs data will depend on the data_type, pointer or fd
typedef struct event {
  epoll_data_t data; // union
  struct event *
      *self_ptr; // this will be an element of active_events array, used to
                 // deactive/remove from active_events(just make this NULL)
  DataType data_type;
} Event;

// helper struct to organize client and server communication
// this will be the pointer that is added to epoll data
typedef struct connection {
  char client_buffer[BUFFER_SIZE], upstream_buffer[BUFFER_SIZE];
  struct sockaddr_storage client_addr; // filled by accept()

  State state;

  int client_fd;
  Str client_headers;   // will point to the length of one full request headers,
                        // client_buffer may contain more bytes than this
  ptrdiff_t read_index; // where to start reading again
  size_t to_read;       // more bytes to read, incase content-length is provided
  bool chunked;         // transfer encoding
  ptrdiff_t next_index; // incase 2 requests arrive back to back
  bool headers_found;   // if nothing more is needed to be read from the
                        // current request, stop reading if new request is
                        // detected
  char last_chunk_found[sizeof LAST_CHUNK]; // how much of the last chunk was
                                            // read
  uint client_status;

  Str upstream_response, request_host, request_path, http_ver, connection;
  int upstream_fd;
} Connection;

// Returns a pointer to Event that needs to be added to the epoll_instance
Event *init_event(DataType data_type, epoll_data_t data);

void free_event(Event **event);

Connection *init_connection(void);

void free_connection(Connection **conn);

// frees both event and the connection it points to
void free_event(Event **event);

void free_event_conn(Event **event);

// adds event to the active_conns array
bool activate_event(Event *event);

// removes event from active_events array
// by making self_ptr NULL which make the array entry NULL
void deactivate_event(Event *event);

// calls fcntl to set non block option on a socket
bool set_non_block(int fd);

// calls epoll_ctl with EPOLL_CTL_ADD
bool add_to_epoll(Event *event, int flags);

// epoll_ctl with EPOLL_CTL_MOD
bool mod_in_epoll(Event *event, int flags);

// epoll_ctl with EPOLL_CTL_DEL
bool del_from_epoll(Event *event);
