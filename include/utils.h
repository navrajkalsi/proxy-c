#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "main.h"

// string helper
typedef struct str {
  char *data;
  ptrdiff_t len;
} Str;

// error node, with name of the errored funciton and its error description
// typically from strerror()
typedef struct errorNode {
  char *function; // function name that errored
  char *error;    // error description
  time_t time;    // time of error
  struct errorNode *next;
} ErrorNode;

// mallocs ErrorNode & inits the values
ErrorNode *init_error_node(const char *function, const char *error);

// adds error to errorList, by creating a new errorNode
void enqueue_error(const char *function, const char *error);

void enqueue_null_error(const char *function);

void print_error_list(void);

// calls free for each node and its vars
void free_error_list(void);

// Always returns false
// for printing non-fatal errors
bool err(const char *error, bool print_errno);

bool null_ptr(const char *error);
