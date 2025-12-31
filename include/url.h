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

typedef struct origin
{
  Str protocol, host, port;
} Origin;

bool parse_url(Str str, URL *url);

Origin parse_origin(Str str);
