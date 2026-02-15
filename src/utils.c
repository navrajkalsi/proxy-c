#include <assert.h>
#include <openssl/ssl.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "connection.h"
#include "proxy.h"
#include "str.h"
#include "utils.h"

bool err(const char *function, const char *error)
{
  if (function && error)
    fprintf(stderr, BOLD_RED "%s()" RESET ": %s\n", function, error);
  else if (function)
    fprintf(stderr, BOLD_RED "%s()\n" RESET, function);
  else
    fputs(BOLD_RED "Unknown error" RESET, stderr);

  return false;
}

void *err_null(const char *function, const char *error)
{
  err(function, error);
  return NULL;
}

void err_n_exit(const char *function, const char *error)
{
  err(function, error);
  exit(EXIT_FAILURE);
}

bool warn(const char *function, const char *warning)
{
  if (!config.log_warnings)
    return false;

  if (function && warning)
    fprintf(stderr, BOLD_YELLOW "%s()" RESET ": %s\n", function, warning);
  else if (function)
    fprintf(stderr, BOLD_YELLOW "%s()\n" RESET, function);
  else
    fputs(BOLD_YELLOW "Unknown warning" RESET, stderr);

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
}

void handle_sigpipe(int sig)
{
  (void)sig;
  puts("\nReceived SIGPIPE signal");
}

void print_active_num(void)
{
  printf("Num of active connections: %d\n", active_conns_num);
}

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

const char *get_state_string(int state)
{
  switch (state)
  {
  case ACCEPT_CLIENT:
    return "accept_client";
  case PEEK_CLIENT:
    return "peek_client";
  case TLS_CLIENT:
    return "tls_client";
  case READ_REQUEST:
    return "read_request";
  case WRITE_ERROR:
    return "write_error";
  case CONNECT_UPSTREAM:
    return "connect_upstream";
  case TLS_UPSTREAM:
    return "tls_upstream";
  case WRITE_REQUEST:
    return "write_request";
  case READ_RESPONSE:
    return "read_response";
  case WRITE_RESPONSE:
    return "write_response";
  case CHECK_CONN:
    return "check_conn";
  case SSL_READ:
    return "ssl_read";
  case SSL_WRITE:
    return "ssl_write";
  case CONN_TIMEDOUT:
    return "conn_timedout";
  case STATE_TIMEDOUT:
    return "state_timedout";
  case CLOSE_CONN:
    return "close_conn";
  default:
    assert(false);
  }
}

int get_active_conn_index(const Connection *conn)
{
  assert(conn);

  for (int i = 0; i < active_conns_num; i++)
    if (active_conns[i] == conn)
      return ++i;

  return 0;
}

void log_state(const char *point_of_call, const Connection *conn)
{
  assert(conn);
  printf("(%d) " BOLD_BLUE "%s" RESET ": %s\n", get_active_conn_index(conn),
         get_state_string(conn->state), point_of_call ? point_of_call : "NULL");
}

void log_ssl_error(int ssl_get_error_ret)
{
  switch (ssl_get_error_ret)
  {
  case SSL_ERROR_NONE:
    puts("SSL_ERROR_NONE");
    break;
  case SSL_ERROR_ZERO_RETURN:
    puts("SSL_ERROR_ZERO_RETURN");
    break;
  case SSL_ERROR_WANT_READ:
    puts("SSL_ERROR_WANT_READ");
    break;
  case SSL_ERROR_WANT_WRITE:
    puts("SSL_ERROR_WANT_WRITE");
    break;
  case SSL_ERROR_WANT_CONNECT:
    puts("SSL_ERROR_WANT_CONNECT");
    break;
  case SSL_ERROR_WANT_ACCEPT:
    puts("SSL_ERROR_WANT_ACCEPT");
    break;
  case SSL_ERROR_WANT_X509_LOOKUP:
    puts("SSL_ERROR_WANT_X509_LOOKUP");
    break;
  case SSL_ERROR_WANT_ASYNC:
    puts("SSL_ERROR_WANT_ASYNC");
    break;
  case SSL_ERROR_WANT_ASYNC_JOB:
    puts("SSL_ERROR_WANT_ASYNC_JOB");
    break;
  case SSL_ERROR_WANT_CLIENT_HELLO_CB:
    puts("SSL_ERROR_WANT_CLIENT_HELLO_CB");
    break;
  case SSL_ERROR_SYSCALL:
    puts("SSL_ERROR_SYSCALL");
    break;
  case SSL_ERROR_SSL:
    puts("SSL_ERROR_SSL");
    break;
  }
}

// thanks to u/skeeto for all the help
bool str_to_size(Str str, ptrdiff_t *num)
{
  assert(num);

  if (!str.len)
    return err("verify_str_len", "Empty str passed");

  ptrdiff_t r = 0;

  for (ptrdiff_t i = 0; i < str.len; i++)
  {
    uint8_t d = (uint8_t)str.data[i] - '0';

    if (d > 9) // no need to check for < 0 as the result of subtraction wraps
      return err("check_digit", "Invalid digit detected");

    if (r > (PTRDIFF_MAX - d) / 10)
      return err("check_overflow", "Overflow detected");

    r = r * 10 + d;
  }

  *num = r;

  return true;
}

bool str_to_size_hex(Str str, ptrdiff_t *num)
{
  assert(num);

  if (str.len > 2)
  {
    if (*str.data != '0' || (str.data[1] != 'x' && str.data[1] != 'X'))
      warn("verify_prefix", "Prefix not detected for hex");
    else
    {
      str.len -= 2;
      str.data += 2;
    }
  }

  ptrdiff_t r = 0;

  for (ptrdiff_t i = 0; i < str.len; i++)
  {
    uint8_t d = (uint8_t)str.data[i];

    if (d >= '0' && d <= '9')
      d -= '0';
    else if (d >= 'a' && d <= 'f')
      d = (uint8_t)(d - 'a') + 10;
    else if (d >= 'A' && d <= 'F')
      d = (uint8_t)(d - 'A') + 10;
    else
      return err("verify_char", "Invalid character detected");

    if (r > (PTRDIFF_MAX - d) / 16)
      return err("check_overflow", "Overflow detected");

    r = r * 16 + d;
  }

  *num = r;

  return true;
}

bool set_date_str(Str date)
{
  assert(date.len == DATE_LEN);

  time_t now = time(NULL);
  struct tm tm;
  gmtime_r(&now, &tm);

  // strftime returns 0 if write buffer is small
  return (bool)strftime(date.data, (size_t)DATE_LEN, "%a, %d %b %Y %H:%M:%S GMT", &tm);
}
