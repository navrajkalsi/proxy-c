#include <assert.h>

#include "main.h"
#include "url.h"

URL parse_url(Str str)
{
  URL url = {.protocol = NULL_STR,
             .host = NULL_STR,
             .port = NULL_STR,
             .path = NULL_STR,
             .params = NULL_STR,
             .frags = NULL_STR};

  if (!str.len)
    return url;

  Str delimiters[] = {STR("://"), STR(":"), STR("/"), STR("?"), STR("#")};
  ptrdiff_t delimiters_len = sizeof delimiters / sizeof(struct str);

  Str next = str;

  for (ptrdiff_t i = 0; i < delimiters_len && next.len; ++i)
  {
    // only try to cut params and frags if host is found ( / or : was detected before)
    if (i == 3 || i == 4)
      assert(url.host.len);

    Str delimiter = delimiters[i];
    Cut cut = cut_str(next, delimiter);

    if (!cut.found)
      continue;

    // Change here in future, if delimiters array changes
    if (i == 0)
    {
      url.protocol = cut.head;
      url.host = cut.tail;
    }
    else if (i == 1)
    {
      url.host = cut.head;
      url.port = cut.tail;
    }
    else if (i == 2)
    { // host can be delimited by : and /, if / is detected and host is null, then the head is
      // choosen as host
      if (!url.host.len)
        url.host = cut.head;
      else
        url.port = cut.head;
      url.path = cut.tail;
    }
    else if (i == 3)
    {
      url.path = cut.head;
      url.params = cut.tail;
    }
    else
    {
      url.params = cut.head;
      url.frags = cut.tail;
    }

    next = cut.tail;
  }

  return url;
}
