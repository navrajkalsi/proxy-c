#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "connection.h"
#include "main.h"
#include "utils.h"

void str_print(const Str *in)
{
  if (in)
    printf("%.*s\n", (int)in->len, in->data);
}

bool equals(const Str a, const Str b)
{
  return a.len == b.len && !memcmp(a.data, b.data, (size_t)(a.len));
}

// returns 0 len str in case of error
Str takehead(Str str, ptrdiff_t take)
{
  if (!str.data || str.len < 0)
    return ERR_STR;

  str.len = take > str.len ? str.len : take;
  return str;
}

Str drophead(Str str, ptrdiff_t drop)
{
  if (!str.data || str.len < 0 || drop > str.len)
    return ERR_STR;

  str.data += drop;
  str.len -= drop;
  return str;
}

Cut cut(Str str, char sep)
{
  ptrdiff_t pos = 0;

  while (pos < str.len && str.data[pos] != sep)
    pos++;

  Cut ret = {};
  ret.found = pos < str.len;
  ret.head = takehead(str, pos);
  ret.tail = drophead(str, pos + ret.found);

  return ret;
}

ptrdiff_t contains(const Str *str, const char *chars)
{
  ptrdiff_t len = (ptrdiff_t)strlen(chars);
  if (!str->len || !str->data || !len || len > str->len)
    return -1;

  for (int i = 0; i < str->len - len; i++)
    if (memcmp(str->data + i, chars, (size_t)len) == 0)
      return i;

  return -1;
}

bool err(const char *function, const char *error)
{
  if (function && error && strcmp(error, strerror(0)))
    fprintf(stderr, "\033[1;31m%s()\033[0m: %s\n", function, error);
  else if (function)
    fprintf(stderr, "\033[1;31m%s()\033[0m\n", function);
  else
    fputs("\033[1;31mUnknown error\033[0m", stderr);

  return false;
}

bool warn(const char *function, const char *warning)
{
  if (!config.log_warnings)
    return false;

  if (function && warning)
    fprintf(stderr, "\033[1;33m%s()\033[0m: %s\n", function, warning);
  else if (function)
    fprintf(stderr, "\033[1;33m%s()\033[0m\n", function);
  else
    fputs("\033[1;33mUnknown warning\033[0m", stderr);

  return false;
}

bool null_ptr(const char *error) { return err(error, strerror(EFAULT)); }

bool set_efault()
{
  errno = EFAULT;
  return false;
}

bool setup_sig_handler(void)
{
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
  if (sigaction(SIGINT, &sa_shutdown, NULL) == -1 || sigaction(SIGTERM, &sa_shutdown, NULL) == -1 ||
      sigaction(SIGPIPE, &sa_pipe, NULL) == -1)
    return err("sigaction", strerror(errno));

  return true;
}

void handle_shutdown(int sig)
{
  (void)sig;
  puts("\nReceived kill signal");
  RUNNING = false;
  return;
}

void handle_sigpipe(int sig)
{
  (void)sig;
  puts("\nReceived SIGPIPE signal");
  return;
}

void print_active_num(void) { printf("Num of active connections: %d\n", active_conns_num); }

void print_banner(void)
{
  puts("\033[1;37m");
  puts(" ██████╗ ██████╗  ██████╗ ██╗  ██╗██╗   ██╗      ██████╗");
  puts(" ██╔══██╗██╔══██╗██╔═══██╗╚██╗██╔╝╚██╗ ██╔╝     ██╔════╝");
  puts(" ██████╔╝██████╔╝██║   ██║ ╚███╔╝  ╚████╔╝█████╗██║     ");
  puts(" ██╔═══╝ ██╔══██╗██║   ██║ ██╔██╗   ╚██╔╝ ╚════╝██║     ");
  puts(" ██║     ██║  ██║╚██████╔╝██╔╝ ██╗   ██║        ╚██████╗");
  puts(" ╚═╝     ╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═╝   ╚═╝         ╚═════╝");
  puts("\033[0m");
}

// does not handle 0 & only works for positive num
void int_to_string(int num, char *out)
{
  static ptrdiff_t pos = 0; // the chars have to be written from the beginning, therefore this
                            // would serve as the index where the char would go

  pos = 0; // may have a value from previous calls

  // base case, when last single int is divided by 10, 0 is returned
  if (!num)
    return;

  int_to_string(num / 10, out);
  *(out + pos++) = (char)((num % 10) + '0');
}

bool compile_regex()
{
  memset(&origin_regex, 0, sizeof origin_regex);
  int status = 0;
  char error_string[256];

  if ((status = regcomp(&origin_regex, ORIGIN_REGEX, REG_EXTENDED | REG_NOSUB | REG_ICASE)) != 0)
  {
    regerror(status, &origin_regex, error_string, sizeof error_string);
    return err("regcomp", error_string);
  }

  return true;
}

bool exec_regex(const regex_t *regex, const char *match)
{
  if (!regex || !match)
    return set_efault();

  int status = 0;
  char error_string[256];

  if ((status = regexec(regex, match, 0, NULL, 0)) != 0)
  {
    regerror(status, regex, error_string, sizeof error_string);
    return err("regexec", error_string);
  }

  return true;
}

const char *get_state_string(int state)
{
  switch (state)
  {
  case ACCEPT_CLIENT:
    return "accept conn";
  case READ_REQUEST:
    return "read_request";
  case VERIFY_REQUEST:
    return "verify_request";
  case WRITE_ERROR:
    return "write_error";
  case CONNECT_UPSTREAM:
    return "connect_upstream";
  case WRITE_REQUEST:
    return "write_request";
  case READ_RESPONSE:
    return "read_response";
  case WRITE_RESPONSE:
    return "write_response";
  case CHECK_CONN:
    return "check_conn";
  case CLOSE_CONN:
    return "close_conn";
  default:
    return "Unknown state";
  }
}

void log_state(int state) { puts(get_state_string(state)); }
