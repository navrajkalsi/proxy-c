#pragma once

#include "str.h"

typedef enum url_delimiters
{
  COLON_SLASHES,
  COLON,
  SLASH,
  QUES,
  HASH,
  DELIMITERS_LEN
} URL_DELIMITERS;

// array of delimiters corresponding with url_delimiters
const Str delimiters[DELIMITERS_LEN];

typedef struct url
{
  Str protocol, host, port, path, params, frags;
} URL;

bool parse_url(Str str, URL *url);

bool validate_url(const URL *url);

// if the protocol matches http or https (only web protocols)
bool validate_protocol(Str protocol);

// is host reachable
bool validate_host(Str host);

// can also be used to get the port in long, if successfull
bool validate_port(Str port, long *port_num);
