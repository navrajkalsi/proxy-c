#pragma once

#include "proxy.h" //

#define VERSION "2.0"

#ifndef DEFAULT_PORT
#define DEFAULT_PORT "1419"
#endif
#ifndef DEFAULT_CANONICAL_HOST // host header to look for in requests
#define DEFAULT_CANONICAL_HOST "https://example.com"
#endif
#ifndef DEFAULT_UPSTREAM // server to contact, can be different from canonical host
#define DEFAULT_UPSTREAM DEFAULT_CANONICAL_HOST
#endif
#define ORIGIN_REGEX                                                                               \
  "^(https?:\\/\\/)?(www\\.)?(localhost|[-[:alnum:]]+(\\.[[:alpha:]]{2,})+)(:[[:digit:]]+)?\\/?$"

#define FALLBACK_HTTP_VER "HTTP/1.1"
#define SERVER "Proxy-C/" VERSION " (Unix)"
#define DATE_LEN 30 // len of date + a null terminator

#define ERR_STR (Str){NULL, 0}
#define NULL_STR (Str){NULL, 0}
#define STR(str)                                                                                   \
  (Str)                                                                                            \
  {                                                                                                \
    str, (ptrdiff_t)(sizeof(str) - 1)                                                              \
  }
#define WRAP_STR(str)                                                                              \
  (Str)                                                                                            \
  {                                                                                                \
    str, strlen(str)                                                                               \
  }

#define BUFFER_SIZE (size_t)8192
#define MB (size_t)1048576
#define READ_FLAGS (int)(EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLHUP | EPOLLRDHUP | EPOLLERR)
#define WRITE_FLAGS (int)(EPOLLOUT | EPOLLET | EPOLLONESHOT | EPOLLHUP | EPOLLRDHUP | EPOLLERR)
#define ERROR_FLAGS (int)(EPOLLHUP | EPOLLRDHUP | EPOLLERR)

#define BACKLOG 25
#define MAX_EVENTS 32
#define MAX_CONNECTIONS 256
#define FALLBACK_UPSTREAM_PORT "80" // change this to 443 after SSL
#ifndef DOMAIN_CERT
#define DOMAIN_CERT "/etc/ssl/domain/domain.cert"
#endif
#ifndef PRIVATE_KEY
#define PRIVATE_KEY "/etc/ssl/domain/private.key"
#endif

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

#define EXPIRES(timeout_p)                                                                         \
  (timeout_p->ttl > (now - timeout_p->start)                                                       \
       ? timeout_p->ttl - (now - timeout_p->start)                                                 \
       : 0) // 'now' should be already defined as time(NULL) in the same scope

#define HTTP STR("http")
#define HTTPS STR("https")

extern Config config;
extern bool RUNNING;
