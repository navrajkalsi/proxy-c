#include <errno.h>  //
#include <signal.h> //
#include <stddef.h> //
#include <stdio.h>  //
#include <stdlib.h> //
#include <string.h> //

#include "proxy.h" //
#include "utils.h" //

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
    fprintf(stderr, "\033[1;33m%s()\033[0m: %s\n", function, warning);
  else if (function)
    fprintf(stderr, "\033[1;33m%s()\033[0m\n", function);
  else
    fputs("\033[1;33mUnknown warning\033[0m", stderr);

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
  // printf("Num of active connections: %d\n", active_conns_num);
  return;
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

// const char *get_state_string(int state)
// {
//   switch (state)
//   {
//   case ACCEPT_CLIENT:
//     return "accept conn";
//   case TLS_CLIENT:
//     return "tls_client";
//   case READ_REQUEST:
//     return "read_request";
//   case VERIFY_REQUEST:
//     return "verify_request";
//   case WRITE_ERROR:
//     return "write_error";
//   case CONNECT_UPSTREAM:
//     return "connect_upstream";
//   case TLS_UPSTREAM:
//     return "tls_upstream";
//   case WRITE_REQUEST:
//     return "write_request";
//   case READ_RESPONSE:
//     return "read_response";
//   case WRITE_RESPONSE:
//     return "write_response";
//   case CHECK_CONN:
//     return "check_conn";
//   case CLOSE_CONN:
//     return "close_conn";
//   default:
//     return "Unknown state";
//   }
// }
//
// void log_state(int state)
// {
//   puts(get_state_string(state));
// }
