#pragma once

#include <stdbool.h>

#define VERSION "0.1"

// args.h specific
#ifndef DEFAULT_PORT
#define DEFAULT_PORT "1419"
#endif
#ifndef DEFAULT_UPSTREAM
#define DEFAULT_UPSTREAM "https://domain.com"
#endif
#ifndef URL_REGEX
#define URL_REGEX "^https?:\\/\\/(www\\.)?[[:alnum:]-]+\\.[[:alpha:]]{2,}\\/?$"
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
#define MAX_EVENTS 256

// request.h specific
#define TRAILER STR("\r\n\r\n")
#define LINEBREAK STR("\r\n")

extern bool RUNNING;
