#pragma once

#include "str.h"

typedef struct url
{
  Str protocol, host, port, path, params, frags;
} URL;

typedef struct origin
{
  Str protocol, host, port;
} Origin;

URL parse_url(Str str);

Origin parse_origin(Str str);
