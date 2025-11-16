#include <assert.h>
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
  if (function && error && strcmp(error, strerror(0)))
    fprintf(stderr, "\033[1;31m%s()\033[0m: %s\n", function, error);
  else if (function)
    fprintf(stderr, "\033[1;31m%s()\033[0m\n", function);
  else
    fputs("\033[1;31mUnknown error\033[0m", stderr);

  return false;
}

bool warn(const char *function, const char *warning) {
  if (function && warning)
    fprintf(stderr, "\033[1;33m%s()\033[0m: %s\n", function, warning);
  else if (function)
    fprintf(stderr, "\033[1;33m%s()\033[0m\n", function);
  else
    fputs("\033[1;33mUnknown warning\033[0m", stderr);

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

// does not handle 0 & only works for positive num
void int_to_string(int num, char *out) {
  static ptrdiff_t pos =
      0; // the chars have to be written from the beginning, therefore this
         // would serve as the index where the char would go

  pos = 0; // may have a value from previous calls

  // base case, when last single int is divided by 10, 0 is returned
  if (!num)
    return;

  int_to_string(num / 10, out);
  *(out + pos++) = (char)((num % 10) + '0');
}

bool compile_regex() {
  memset(&origin_regex, 0, sizeof origin_regex);
  int status = 0;
  char error_string[256];

  if ((status = regcomp(&origin_regex, ORIGIN_REGEX,
                        REG_EXTENDED | REG_NOSUB | REG_ICASE)) != 0) {
    regerror(status, &origin_regex, error_string, sizeof error_string);
    return err("regcomp", error_string);
  }

  return true;
}

bool exec_regex(const regex_t *regex, const char *match) {
  if (!regex || !match)
    return set_efault();

  int status = 0;
  char error_string[256];

  if ((status = regexec(regex, match, 0, NULL, 0)) != 0) {
    regerror(status, regex, error_string, sizeof error_string);
    return err("regexec", error_string);
  }

  return true;
}

bool pull_buf(Endpoint *endpoint) {
  if (!endpoint)
    return set_efault();

  if (!endpoint->next_index) // nothing to do, read new request
    return true;

  assert(endpoint->read_index > endpoint->next_index);

  size_t to_copy = endpoint->read_index - endpoint->next_index;
  memcpy(endpoint->buffer, endpoint->buffer + endpoint->next_index, to_copy);
  endpoint->read_index = to_copy;
  endpoint->to_read = BUFFER_SIZE - endpoint->read_index - 1;
  endpoint->next_index = 0;

  endpoint->headers_found = false;

  return true;
}

bool find_last_chunk(Endpoint *endpoint) {
  if (!endpoint)
    return set_efault();

  assert(endpoint->headers_found);

  // pointer at chars after headers
  char *start = endpoint->buffer + endpoint->buf_view.len;

  if (*start == '\0') // no body
    return false;

  // first checking if already reading last chunk from previous call
  size_t matched = strlen(endpoint->last_chunk_found);

  while (matched && matched < (size_t)LAST_CHUNK_STR.len &&
         *start) {                       // continue to check for last_chunk
    if (*start != LAST_CHUNK[matched]) { // mismatch
      *endpoint->last_chunk_found = '\0';
      matched = 0;
      break;
    }

    endpoint->last_chunk_found[matched] = LAST_CHUNK[matched];
    endpoint->last_chunk_found[++matched] = '\0';

    start++;
  }

  if (matched == (size_t)LAST_CHUNK_STR.len) { // full chunk matched
    if (*start)                                // next request
      endpoint->next_index = start - endpoint->buffer;
    return true;
  } else if (matched) // full chunk not matched but ran out of chars
    return false;     // read more

  // nothing more to compare
  if (!*start)
    return false;

  // last chunk not found in the beginning
  char *last_chunk = LAST_CHUNK;
  if ((last_chunk = strstr(start, last_chunk))) { // last chunk was read
    ptrdiff_t chunk_end = (last_chunk + LAST_CHUNK_STR.len) -
                          endpoint->buffer; // this is past the \n

    if (endpoint->buffer[chunk_end]) // read the body and another request
      endpoint->next_index = chunk_end;

    // read the body and nothing more
    return true;
  }

  // full last chunk not found, check last bytes in the buffer for worst case:
  // '0\r\n\r'
  start = endpoint->buffer + endpoint->buf_view.len;
  size_t read_size = strlen(start), // num of chars available to check at most
      to_match = read_size < (size_t)LAST_CHUNK_STR.len - 1
                     ? read_size
                     : (size_t)LAST_CHUNK_STR.len - 1;
  ptrdiff_t match_index = read_size - to_match;
  matched = 0;

  // checking if 0 is received, only using last to_match bytes
  while (start[match_index]) {
    if (start[match_index] != LAST_CHUNK[matched]) {
      matched = 0;
      *endpoint->last_chunk_found = '\0'; // restart
    } else {
      endpoint->last_chunk_found[matched] = LAST_CHUNK[matched];
      endpoint->last_chunk_found[++matched] = '\0';
    }

    match_index++;
  }

  return false;
}
