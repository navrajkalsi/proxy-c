#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

// string helper
typedef struct str {
  char *data;
  ptrdiff_t len;
} Str;

typedef struct {
  Str head;
  Str tail;
  bool found;
} Cut;

// Converts a null terminated string to a malloced Str
// The str is NOT MALLOCED, just the data
Str str_data_malloc(const char *in);

// The str and data BOTH ARE MALLOCED
Str *str_malloc(const char *in);

void str_data_free(Str *in);

void str_free(Str **in);

void str_print(const Str *in);

// strcmp like
bool equals(const Str a, const Str b);

// returns Str which points to starting of str but with take len, if possible
Str takehead(Str str, ptrdiff_t take);
// since Str args in both are copies, simply could change the str and return
// returns Str which points to str.len - drop, if possible
Str drophead(Str str, ptrdiff_t drop);

// cuts a string around the separator without copying str
// The head and tail are just pointers to the org str with different lengths and
// starting values
Cut cut(Str str, char sep);

// checks if str contains chars anywhere
// returns the index (if found), otherwise 0
ptrdiff_t contains(const Str *str, const char *chars);

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
// always returns false
bool enqueue_error(const char *function, const char *error);

void print_error_list(void);

// calls free for each node and its vars
void free_error_list(void);

// Always returns false
// for printing non-fatal errors
bool err(const char *function, const char *error);

bool null_ptr(const char *error);

// just like null_ptr() but does not print anything
bool set_efault(void);

bool setup_sig_handler(void);

void handle_shutdown(int sig);

void handle_sigpipe(int sig);

// prints number of active connections
void print_active_num(void);

char *get_status_string(int status_code);
