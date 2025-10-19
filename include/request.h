#pragma once

#include "poll.h"

// called after EPOLLIN is detected on a client socketclient conn
bool handle_request(const EventData *event_data);

// verifies the request method, host
bool validate_request(const Str *request);

// takes in the whole host header
bool validate_host(const Str *header);
