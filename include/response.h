#pragma once

#include "poll.h"

// writes code to the client incase not equal to 200
bool write_error_response(EventData *event_data);
