#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "main.h"
#include "proxy.h"
#include "utils.h"

Str str_data_malloc(const char *in) {
  return !in ? ERR_STR
             : (Str){.data = strdup(in), .len = (ptrdiff_t)strlen(in)};
}

Str *str_malloc(const char *in) {
  Str *ret = (Str *)malloc(sizeof *ret);

  if (!ret) {
    err("malloc", strerror(errno));
    return NULL;
  }

  ret->data = in ? strdup(in) : NULL;
  ret->len = in ? (ptrdiff_t)strlen(in) : 0;

  return ret;
}

void str_data_free(Str *in) {
  if (!in || !in->data || !in->len)
    return;

  free(in->data);
  in->data = NULL;
  in->len = 0;
}

void str_free(Str **in) {
  if (!in || !*in)
    return;

  str_data_free(*in);
  free(*in);
  *in = NULL;
}

void str_print(const Str *in) {
  if (in)
    printf("%.*s\n", (int)in->len, in->data);
}

bool equals(const Str a, const Str b) {
  return a.len == b.len && !memcmp(a.data, b.data, (size_t)(a.len));
}

// returns 0 len str in case of error
Str takehead(Str str, ptrdiff_t take) {
  if (!str.data || str.len < 0)
    return ERR_STR;

  str.len = take > str.len ? str.len : take;
  return str;
}

Str drophead(Str str, ptrdiff_t drop) {
  if (!str.data || str.len < 0 || drop > str.len)
    return ERR_STR;

  str.data += drop;
  str.len -= drop;
  return str;
}

Cut cut(Str str, char sep) {
  ptrdiff_t pos = 0;

  while (pos < str.len && str.data[pos] != sep)
    pos++;

  Cut ret = {};
  ret.found = pos < str.len;
  ret.head = takehead(str, pos);
  ret.tail = drophead(str, pos + ret.found);

  return ret;
}

ptrdiff_t contains(const Str *str, const char *chars) {
  ptrdiff_t len = (ptrdiff_t)strlen(chars);
  if (!str->len || !str->data || !len || len > str->len)
    return -1;

  for (int i = 0; i < str->len - len; i++)
    if (memcmp(str->data + i, chars, (size_t)len) == 0)
      return i;

  return -1;
}

// error linked list head and tail nodes
ErrorNode *error_head = NULL, *error_tail = NULL;

ErrorNode *init_error_node(const char *function, const char *error) {
  ErrorNode *node;

  if (!(node = malloc(sizeof(ErrorNode)))) {
    err("malloc", strerror(errno));
    return NULL;
  }

  node->function = function ? strdup(function) : NULL;
  node->error = error && strcmp(error, "Success") ? strdup(error) : NULL;
  node->time = time(NULL);
  node->next = NULL;

  return node;
}

bool enqueue_error(const char *function, const char *error) {
  if (!function)
    return false;

  ErrorNode *node;
  if (!(node = init_error_node(function, error)))
    return err("init_error_node", NULL);

  // adding to the list
  if (error_tail)
    error_tail = error_tail->next = node;
  else
    error_head = error_tail = node;

  return false;
}

void print_error_list(void) {
  if (!error_head)
    return;

  {
    fputs("\n", stderr);
    for (int i = 0; i < TERMINAL_WIDTH; ++i)
      fputs("~", stderr);
    fputs("\nError Queue:\n\n", stderr);
  }

  ErrorNode *current = error_head;
  do {
    struct tm *time = localtime(&current->time);

    // zero padding with fixed width of 2 in time
    fprintf(stderr, "[%02d:%02d:%02d] %s()", time->tm_hour, time->tm_min,
            time->tm_sec, current->function);
    if (current->error)
      fprintf(stderr, ": %s\n", current->error);
    else
      fputs("\n", stderr);
  } while ((current = current->next));

  for (int i = 0; i < TERMINAL_WIDTH; ++i)
    fputs("~", stderr);
  puts("\n");
}

void free_error_list(void) {
  if (!error_head)
    return;

  ErrorNode *current = error_head;

  do {
    ErrorNode *next = current->next;

    if (current->function)
      free(current->function);
    if (current->error)
      free(current->error);

    free(current);
    current = next;

  } while (current);

  error_head = error_tail = NULL;
}

bool err(const char *function, const char *error) {
  // this function is used for immediate error reporting, therefore if any child
  // functions enqueued to error_list, those errors will not be required any
  // more
  free_error_list();
  if (function && error && strcmp(error, strerror(0)))
    fprintf(stderr, "%s(): %s\n", function, error);
  else if (function)
    fprintf(stderr, "%s()\n", function);
  else
    fputs("Unknown error", stderr);

  return false;
}

bool null_ptr(const char *error) { return err(error, strerror(EFAULT)); }

bool set_efault() {
  errno = EFAULT;
  return false;
}

bool setup_sig_handler(void) {
  struct sigaction sa_shutdown, sa_pipe;

  // Shutdown
  sa_shutdown.sa_handler = handle_shutdown;
  sigemptyset(&sa_shutdown.sa_mask);
  sa_shutdown.sa_flags = 0; // No flags required for shutting down

  // SIGPIPE
  sa_pipe.sa_handler = handle_sigpipe;
  sigemptyset(&sa_pipe.sa_mask);
  sa_pipe.sa_flags = 0;

  // SIGINT (signal interput) is sent when Ctrl+C is pressed
  // SIGTERM (signal terminate) is sent when the process is killed from like
  // terminal with kill command
  if (sigaction(SIGINT, &sa_shutdown, NULL) == -1 ||
      sigaction(SIGTERM, &sa_shutdown, NULL) == -1 ||
      sigaction(SIGPIPE, &sa_pipe, NULL) == -1)
    return err("sigaction", strerror(errno));

  return true;
}

void handle_shutdown(int sig) {
  (void)sig;
  puts("\nReceived kill signal");
  RUNNING = false;
  return;
}

void handle_sigpipe(int sig) {
  (void)sig;
  puts("\nReceived SIGPIPE signal");
  return;
}

void print_active_num(void) {
  int active = 0;
  for (int i = 0; i < MAX_CONNECTIONS; ++i)
    if (active_conns[i])
      active++;

  printf("Num of active connections: %d\n", active);
}

char *get_status_string(int status_code) {
  // these strings live for the entire life of the program
  switch (status_code) {
  case 200:
    return "200 OK";
  case 301:
    return "301 Moved Permanently";
  case 400:
    return "400 Bad Request";
  case 403:
    return "403 Forbidden";
  case 404:
    return "404 Not Found";
  case 405:
    return "405 Method Not Allowed";
  case 431:
    return "431 Request Header Fields Too Large";
  case 500:
    return "500 Internal Server Error";
  case 505:
    return "505 HTTP Version Not Supported";
  }

  return "500 Internal Server Error";
}
