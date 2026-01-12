#define VERSION "2.0"

#define STR(chars)                                                                                 \
  (Str)                                                                                            \
  {                                                                                                \
    chars, (ptrdiff_t)(sizeof(chars) - 1)                                                          \
  }
#define WRAP_STR(chars)                                                                            \
  (Str)                                                                                            \
  {                                                                                                \
    chars, (ptrdiff_t)strlen(chars)                                                                \
  }
#define NULL_STR (Str){NULL, 0}
#define HTTPS STR("https")
#define HTTP STR("http")
#define CRLF STR("\r\n")
#define LAST_CHUNK STR("0\r\n\r\n")
#define TRAILER STR("\r\n\r\n")

#ifndef DEFAULT_LISTEN_PORT
#define DEFAULT_LISTEN_PORT "1419"
#endif
#ifndef DEFAULT_CANONICAL_HOST
#define DEFAULT_CANONICAL_HOST "example.com"
#endif
#ifndef DEFAULT_UPSTREAM_HOST
#define DEFAULT_UPSTREAM_HOST DEFAULT_CANONICAL_HOST
#endif

#define BUFFER_SIZE (size_t)8192
#define MAX_CHUNKED_HEAD (size_t)18
#define MB (size_t)1048576
#define READ_FLAGS (int)(EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLHUP | EPOLLRDHUP | EPOLLERR)
#define WRITE_FLAGS (int)(EPOLLOUT | EPOLLET | EPOLLONESHOT | EPOLLHUP | EPOLLRDHUP | EPOLLERR)
#define ERROR_FLAGS (int)(EPOLLHUP | EPOLLRDHUP | EPOLLERR)
#define TIMER_FLAGS (int)(EPOLLIN | EPOLLET | EPOLLONESHOT)
#define BACKLOG 25
#define MAX_EVENTS 32
#define MAX_CONNECTIONS 256
#ifndef DOMAIN_CERT
#define DOMAIN_CERT "/etc/ssl/domain/domain.cert"
#endif
#ifndef PRIVATE_KEY
#define PRIVATE_KEY "/etc/ssl/domain/private.key"
#endif

#define BOLD "\033[1m"
#define BOLD_RED "\033[1;31m"
#define BOLD_YELLOW "\033[1;33m"
#define RESET "\033[0m"
#define BOLDEN(chars) BOLD chars RESET
