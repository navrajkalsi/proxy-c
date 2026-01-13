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

typedef struct url
{
  Str unparsed, protocol, host, port, path, params, frags;
} URL;

typedef struct host
{
  Str unparsed, host, port;
} Host;

bool parse_url(Str str, URL *url);

// fills fields of host from url
void extract_host(URL *url, Host *host);

// if the protocol matches http or https (only web protocols)
bool validate_protocol(Str protocol);

// is host reachable
bool validate_host(Str host);

// can also be used to get the port in long, if successfull and port_out is a valid pointer
bool validate_port(Str port, long *port_out);
