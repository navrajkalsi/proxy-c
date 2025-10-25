#pragma once

#include <stdbool.h>

#include "poll.h"

// sets required status codes
bool validate_request(Connection *conn);

// takes in the value of host header and also compares it to the upstream
bool validate_host(const Str *header);

bool validate_method(const Str method);

bool validate_http(const Str http_ver);

// finds header_name from headers and points header_value to a Str containing
// the value of the request header or returns false if the header is not found
bool get_header_value(const char *headers, const char *header_name,
                      Str *header_value);

// for logging request to stdout
void print_request(const Connection *conn);
