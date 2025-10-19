#pragma once

#include "poll.h"

// called after EPOLLIN is detected on a client socketclient conn
bool handle_request(const EventData *event_data);

// verifies the request method & sets required status
bool validate_request(Connection *conn);

// takes in the value of host header and also compares it to the upstream
bool validate_host(const Str *header);
