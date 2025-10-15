#include <asm-generic/errno-base.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "utils.h"

// error linked list head and tail nodes
ErrorNode *error_head = NULL, *error_tail = NULL;

ErrorNode *init_error_node(const char *function, const char *error) {
  ErrorNode *node;

  if (!(node = malloc(sizeof(ErrorNode)))) {
    err("malloc", strerror(errno));
    return NULL;
  }

  node->function = function ? strdup(function) : NULL;
  node->error = error ? strdup(error) : NULL;
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
  if (function && !error)
    fprintf(stderr, "%s(): %s\n", function, error);
  else if (function)
    fprintf(stderr, "%s()\n", function);

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
    return enqueue_error("sigaction", strerror(errno));

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
