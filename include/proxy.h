#pragma once

#include <sys/epoll.h>

#include "args.h"

// if connection is storing a fd (only in case of listening sock) or ptr
typedef enum { TYPE_FD, TYPE_PTR } DataType;

// struct to be used for adding to the epoll instance
typedef struct event_data {
  epoll_data_t data; // union
  DataType data_type;
} EventData;

// helper struct to organize client and server communication
// this will be the pointer that is added to epoll data
typedef struct connection {
  int client_fd, client_events, server_fd, server_events;
} Connection;

// Returns a pointer to event_data that needs to be added to the epoll_instance
EventData *init_event_data(DataType data_type, epoll_data_t data);

void free_event_data(EventData **event_data);

Connection *init_connection(void);

void free_connection(Connection **conn);

// calls fcntl to set non block option on a socket
bool set_non_block(int fd);

// calls epoll_ctl to add
bool add_to_epoll(int epoll_fd, EventData *data, int flags);

bool setup_proxy(Config *config, int *proxy_fd);

// sets up epoll() and points out to the fd for new epoll instance
bool setup_async(int proxy_fd, int *epoll_fd);

bool start_proxy(int epoll_fd);
