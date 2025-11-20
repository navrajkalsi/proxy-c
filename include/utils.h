#pragma once

#include <regex.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

// string helper
typedef struct str
{
  char *data;
  ptrdiff_t len;
} Str;

typedef struct
{
  Str head;
  Str tail;
  bool found;
} Cut;

void str_print(const Str *in);

// strcmp like
bool equals(const Str a, const Str b);

// returns Str which points to starting of str but with take len, if possible
Str takehead(Str str, ptrdiff_t take);
// since Str args in both are copies, simply could change the str and return
// returns Str which points to str.len - drop, if possible
Str drophead(Str str, ptrdiff_t drop);

// cuts a string around the separator without copying str
// The head and tail are just pointers to the org str with different lengths and starting values
Cut cut(Str str, char sep);

// Always returns false
// for printing non-fatal errors
bool err(const char *function, const char *error);

// non fatal logging
bool warn(const char *function, const char *warning);

bool null_ptr(const char *error);

// just like null_ptr() but does not print anything
bool set_efault(void);

bool setup_sig_handler(void);

void handle_shutdown(int sig);

void handle_sigpipe(int sig);

// prints number of active connections
void print_active_num(void);

// out should point to a memory that can hold the final string
// check before if num is 0, the function does not handle 0 as num
// this function does not malloc!
void int_to_string(int num, char *out);

// for compiling regex for ORIGIN_URL, use at startup once for the lifetime
bool compile_regex(void);

// encapsulating error reporting
bool exec_regex(const regex_t *regex, const char *match);
