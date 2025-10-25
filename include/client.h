#pragma once

#include <stdbool.h>

#include "poll.h"

// calls accept on listening socket fd and adds it to the epoll instance
bool accept_client(int proxy_fd, int epoll_fd);

// called after EPOLLIN is detected on a client socketclient conn
bool handle_request_client(const EventData *event_data);

bool handle_response_client(const EventData *event_data);

bool generate_response(Connection *conn);

bool generate_error_response(Connection *conn);
