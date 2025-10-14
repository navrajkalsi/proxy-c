#include <errno.h>
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
    err("Malloc Error Node", true);
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
    return err("Init Error Node", false);

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

bool err(const char *error, bool print_errno) {
  if (error && print_errno)
    perror(error);
  else if (error)
    fprintf(stderr, "%s\n", error);

  return false;
}

bool null_ptr(const char *error) {
  errno = EFAULT;
  return err(error, true);
}

bool set_efault() {
  errno = EFAULT;
  return false;
}
