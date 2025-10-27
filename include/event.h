#pragma once

#include <stdbool.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "main.h"
#include "utils.h"

// if connection is storing a fd (only in case of listening sock) or ptr
typedef enum { TYPE_FD, TYPE_PTR_CLIENT, TYPE_PTR_UPSTREAM } DataType;

// struct to be used for adding/modding/deleting to the epoll instance
typedef struct event {
  epoll_data_t data; // union
  struct event *
      *self_ptr; // this will be an element of active_conns array, used to
                 // deactive/remove from active_conns(just make this NULL)
  DataType data_type;
} Event;

// helper struct to organize client and server communication
// this will be the pointer that is added to epoll data
typedef struct connection {
  char client_buffer[BUFFER_SIZE], upstream_buffer[BUFFER_SIZE];
  struct sockaddr_storage client_addr;
  Str client_request, upstream_response, request_host, request_path, http_ver,
      connection;
  int client_fd, upstream_fd, client_status;
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

// calls epoll_ctl to add
bool add_to_epoll(int epoll_fd, Event *data, int flags);
