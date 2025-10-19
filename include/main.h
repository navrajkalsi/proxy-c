#pragma once

#include <stdbool.h>

#include "args.h"

#define VERSION "0.2"

// args.h specific
#ifndef DEFAULT_PORT
#define DEFAULT_PORT "1419"
#endif
#ifndef DEFAULT_UPSTREAM
#define DEFAULT_UPSTREAM "https://domain.com"
#endif
#ifndef ORIGIN_REGEX
#define ORIGIN_REGEX                                                           \
  "^(https?:\\/\\/"                                                            \
  ")?(www\\.)?(localhost|[-[:alnum:]]+(\\.[[:alpha:]]{2,})+)(:[[:digit:]]+)?"  \
  "\\/?$"
#endif

// utils.h specific
#ifndef TERMINAL_WIDTH
#define TERMINAL_WIDTH 80
#endif
#define ERR_STR (Str){NULL, 0}
#define STR(str)                                                               \
  (Str) { str, (ptrdiff_t)(sizeof(str) - 1) }

// poll.h specific
#define BUFFER_SIZE 8192

// proxy.h specific
#define BACKLOG 25
#define MAX_EVENTS 32
#define MAX_CONNECTIONS 256

// request.h specific
#define TRAILER STR("\r\n\r\n")
#define LINEBREAK STR("\r\n")
// only to assign the string literal to str.data if str.data is null
#define ASSIGN_IF_NULL(str, literal) !str.data ? STR(literal) : str

extern bool RUNNING;
extern Config config;
