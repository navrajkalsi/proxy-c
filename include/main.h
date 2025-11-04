#pragma once

#include <stdbool.h>

#include "args.h"

#define VERSION "0.5"

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

// http.h specific
#define FALLBACK_HTTP_VER "HTTP/1.1"
#define SERVER "Proxy-C/" VERSION " (Unix)"
#define DATE_LEN 30 // len of date + a null terminator

// utils.h specific
#ifndef TERMINAL_WIDTH
#define TERMINAL_WIDTH 80
#endif
#define ERR_STR (Str){NULL, 0}
#define STR(str)                                                               \
  (Str) { str, (ptrdiff_t)(sizeof(str) - 1) }

// event.h specific
#define BUFFER_SIZE 8192
#define MB 1048576
#define READ_FLAGS                                                             \
  EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLHUP | EPOLLRDHUP | EPOLLERR

// proxy.h specific
#define BACKLOG 25
#define MAX_EVENTS 32
#define MAX_CONNECTIONS 256
#define FALLBACK_UPSTREAM_PORT "80" // change this to 443 after SSL

// request.h specific
#define TRAILER "\r\n\r\n"
#define LINEBREAK "\r\n"
#define SPACE " "
#define LAST_CHUNK "0" TRAILER
#define TRAILER_STR STR(TRAILER)
#define LINEBREAK_STR STR(LINEBREAK)
#define SPACE_STR STR(SPACE)
#define LAST_CHUNK_STR STR(LAST_CHUNK)
// only to assign the string literal to str.data if str.data is null
#define ASSIGN_IF_NULL(str, literal) !str.data ? STR(literal) : str

extern bool RUNNING;
extern Config config;
