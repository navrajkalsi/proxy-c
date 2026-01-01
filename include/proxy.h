#include "url.h"     //
#include <stdbool.h> //

typedef struct config
{
  Origin canonical_origin;
  Str listen_port, upstream;
  bool accept_all, log_warnings, client_https, upstream_https;
} Config;
