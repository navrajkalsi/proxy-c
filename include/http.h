#pragma once

#include <stdbool.h>

#include "connection.h"
#include "utils.h"

// takes in the value of host header and also compares it to the upstream
bool validate_host(const Str *header);

bool validate_method(const Str method);

bool validate_http(const Str http_ver);

// finds header_name from headers and points header_value to a Str containing
// the value of the request header or returns false if the header is not found
bool get_header_value(const char *headers, const char *header_name, Str *header_value);

// date.data should point to a memory of DATE_LEN bytes
// this function does not malloc!
bool set_date_str(Str *date);

// date should point to a memory of DATE_LEN bytes
// this function does not malloc!
bool set_date_string(char *date);

// finds connection header in buffer (can be client's or upstream's) and respects its value
void set_connection(const char *buffer, Connection *conn);

// for logging request to stdout
void print_request(const Connection *conn);

char *get_status_string(int status_code);

Str get_status_str(int status_code);
