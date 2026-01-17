#include <assert.h>
#include <openssl/ssl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "connection.h"
#include "proxy.h"
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
    return "accept conn";
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

void log_state(int state)
{
  puts(get_state_string(state));
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

bool str_to_long(Str str, long *num)
{
  assert(str.len);
  assert(num);

  if (str.len > 64)
    return err("verify_str_len", "Potential number longer than long");

  char local[str.len + 1], *end = NULL;
  memcpy(local, str.data, (size_t)str.len);
  local[str.len] = '\0';
  errno = 0;

  *num = strtol(local, &end, 10);

  if (errno == ERANGE)
    return err("strtol", "Overflow detected");

  if (end == local) // comparing pointers
    return err("strtol", "No conversion performed");

  if (*end != '\0')
    return err("strtol", "Supplied str is not a decimal number");

  return true;
}

bool str_to_long_hex(Str str, long *num)
{
  assert(str.len);
  assert(num);

  if (str.len > 64)
    return err("verify_str_len", "Potential number longer than long");

  char local[str.len + 1], *end = NULL;
  memcpy(local, str.data, (size_t)str.len);
  local[str.len] = '\0';
  errno = 0;

  *num = strtol(local, &end, 16);

  if (errno == ERANGE)
    return err("strtol", "Overflow detected");

  if (end == local) // comparing pointers
    return err("strtol", "No conversion performed");

  if (*end != '\0')
    return err("strtol", "Supplied str is not a hex number");

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
