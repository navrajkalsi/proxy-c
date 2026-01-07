#pragma once

#include <stdbool.h>
#include <stddef.h>

// Thanks to u/skeeto on reddit for this and much more!

typedef struct str
{
  char *data;
  ptrdiff_t len;
} Str;

typedef struct cut
{
  Str head, tail;
  bool found;
} Cut;

void put_str(const Str *str);

// not including char at upto
Str get_head(Str str, ptrdiff_t upto);

// including char at from
Str get_tail(Str str, ptrdiff_t from);

// -1 if not found
ptrdiff_t contains(Str str, Str find);

Cut cut_char(Str str, char sep);

Cut cut_str(Str str, Str sep);

bool equals(Str a, Str b);

char case_fold_char(char c);

Str case_fold_str(Str str);

// case insensitive version of equals
bool case_equals(Str a, Str b);

// removes \r from end
void trim_cr(Str *str);
