#pragma once

#include <stdbool.h>
#include <sys/epoll.h>

#include "main.h"
#include "utils.h"

// if connection is storing a fd (only in case of listening sock) or ptr
typedef enum { TYPE_FD, TYPE_PTR_CLIENT, TYPE_PTR_UPSTREAM } DataType;

typedef enum {
  CLIENT_READ,
  CLIENT_WRITE,
  UPSTREAM_READ,
  UPSTREAM_WRITE
} Operation;

// struct to be used for adding to the epoll instance
typedef struct event_data {
  epoll_data_t data; // union
  struct event_data *
      *self_ptr; // this will be an element of active_conns array, used to
                 // deactive/remove from active_conns(just make this NULL)
  DataType data_type;
} EventData;

// helper struct to organize client and server communication
// this will be the pointer that is added to epoll data
typedef struct connection {
  char client_buffer[BUFFER_SIZE], upstream_buffer[BUFFER_SIZE];
  Str client_request, upstream_response, client_status;
  Operation operation;
  int client_fd, upstream_fd;
} Connection;

// Returns a pointer to event_data that needs to be added to the epoll_instance
EventData *init_event_data(DataType data_type, epoll_data_t data);

void free_event_data(EventData **event_data);

Connection *init_connection(void);

void free_connection(Connection **conn);

// frees both event data and the connection it points to
void free_event_conn(EventData **event_data);

// adds event_data to the active_conns array
bool activate_event(EventData *event_data);

// removes event_data from active_conns array
// by making self_ptr NULL which make the array entry NULL
void deactivate_event(EventData *event_data);

// calls fcntl to set non block option on a socket
bool set_non_block(int fd);

// calls epoll_ctl to add
bool add_to_epoll(int epoll_fd, EventData *data, int flags);
