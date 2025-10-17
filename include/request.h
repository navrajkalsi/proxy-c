#pragma once

#include "poll.h"

// called after EPOLLIN is detected on a client socketclient conn
bool handle_request(EventData *event_data);
