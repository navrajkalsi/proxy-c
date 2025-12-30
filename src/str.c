#include <assert.h>
#include <string.h>

#include "main.h"
#include "str.h"

Str get_head(Str str, ptrdiff_t upto)
{
  assert(str.len >= 0 && upto >= 0);

  str.len = upto < str.len ? upto : str.len;
  return str;
}

Str get_tail(Str str, ptrdiff_t from)
{
  assert(str.len >= 0 && from >= 0 && from <= str.len);

  str.data += from;
  str.len -= from;
  return str;
}

ptrdiff_t contains(Str str, Str find)
{
  if (!str.len || !str.data || !find.len || find.len > str.len)
    return -1;

  for (ptrdiff_t i = 0; i <= str.len - find.len; i++)
    if (memcmp(str.data + i, find.data, (size_t)find.len) == 0)
      return i;

  return -1;
}

Cut cut_char(Str str, char sep)
{
  ptrdiff_t pos = 0;

  while (pos < str.len && str.data[pos] != sep)
    pos++;

  Cut cut = {};
  cut.found = pos < str.len;
  cut.head = get_head(str, pos);
  cut.tail = get_tail(str, pos + cut.found);

  return cut;
}

Cut cut_str(Str str, Str sep)
{
  Cut cut = {};
  ptrdiff_t pos = -1;

  cut.found = (pos = contains(str, sep)) != -1;
  cut.head = cut.found ? get_head(str, pos) : str;
  cut.tail = cut.found ? get_tail(str, pos + sep.len) : STR("");

  return cut;
}

bool equals(Str a, Str b)
{
  return a.len == b.len && !memcmp(a.data, b.data, (size_t)a.len);
}
