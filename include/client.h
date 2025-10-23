#pragma once

#include <stdbool.h>

#include "poll.h"

// calls accept on listening socket fd and adds it to the epoll instance
bool accept_client(int proxy_fd, int epoll_fd);

// called after EPOLLIN is detected on a client socketclient conn
bool handle_request_client(const EventData *event_data);

// verifies the request method & sets required status
bool validate_request(Connection *conn);

// takes in the value of host header and also compares it to the upstream
bool validate_host(const Str *header);

// for logging request to stdout
void print_request(const Connection *conn);

bool handle_response_client(const EventData *event_data);

bool generate_response(Connection *conn);

bool generate_error_response(Connection *conn);
