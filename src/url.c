#include <assert.h>
#include <netdb.h>
#include <string.h>

#include "bits.h"
#include "main.h"
#include "url.h"
#include "utils.h"

// corresponding with url_delimiters enum
const Str delimiters[DELIMITERS_LEN] = {STR("://"), STR(":"), STR("/"), STR("?"), STR("#")};

bool parse_url(Str str, URL *url)
{
  assert(url);
  assert(str.len);
  url->unparsed = str;
  url->protocol = url->host = url->port = url->path = url->params = url->frags = NULL_STR;

  uint8_t tracker = 0u, *tracker_p = &tracker;
  int last_found = -1;
  Str next = str;

  for (ptrdiff_t i = 0; i < DELIMITERS_LEN && next.len; ++i)
  {
    Str delimiter = delimiters[i];
    Cut cut = cut_str(next, delimiter);

    if (!cut.found)
      continue;

    last_found = i;

    switch (i)
    { // only protocol and host are case insensitive
    case COLON_SLASHES:
      url->protocol = case_fold_str(cut.head);
      break;

    case COLON:
      url->host = case_fold_str(cut.head);
      break;

    case SLASH:
      // host can be delimited by : and /, if / is detected and host is null, then the head is
      // choosen as host
      if (read_bit(tracker_p, COLON))
        url->port = cut.head;
      else
        url->host = cut.head;
      url->path = cut.tail;
      break;

    case QUES:
      // host must have been detected at this point
      if (!read_bit(tracker_p, COLON) && !read_bit(tracker_p, SLASH))
        return err("parse_url_switch", "Malformed URL");
      url->path = cut.head;
      break;

    case HASH:
      if (!read_bit(tracker_p, COLON) && !read_bit(tracker_p, SLASH))
        return err("parse_url_switch", "Malformed URL");
      if (read_bit(tracker_p, QUES))
        url->params = cut.head;
      else
        url->path = cut.head;
      url->frags = cut.tail;
      break;

    default:
      assert(false);
    }

    set_bit(tracker_p, i);

    next = cut.tail;

    // only path can be empty, rest of the sections need something
    if (i == QUES)
      continue;

    if (!cut.head.len)
      return err("parse_url_verify", "Empty section detected in the URL");
  }

  // no delimiter found, treat the whole str as host
  if (!tracker)
    url->host = str;
  else
  {
    if (!next.len && last_found != SLASH) // last section empty, only path can be empty
      return err("parse_url_verify", "Empty section detected in the URL");

    // assign last section
    switch (last_found)
    {
    case COLON_SLASHES:
      url->host = next;
      break;
    case COLON:
      url->port = next;
      break;
    case SLASH:
      url->path = next;
      break;
    case QUES:
      url->params = next;
      break;
    case HASH:
      url->frags = next;
      break;
    }
  }

  return true;
}

void extract_host(URL *url, Host *host)
{
  assert(url && host);

  host->unparsed = url->unparsed;
  host->host = url->host;
  host->port = url->port;
}

bool validate_protocol(Str protocol)
{
  return case_equals(protocol, HTTPS) || case_equals(protocol, HTTP);
}

bool validate_host(Str host)
{
  char string[host.len + 1]; // null terminated hostname
  memcpy(string, host.data, host.len);
  string[host.len] = '\0';

  struct addrinfo *res;
  int status = getaddrinfo(string, NULL, NULL, &res);

  if (res)
    freeaddrinfo(res);

  return status == 0 || err("getaddrinfo", gai_strerror(status));
}

bool validate_port(Str port, long *port_out)
{
  assert(port.len);

  long port_num = -1;

  if (!str_to_long(port, &port_num))
    return err("str_to_long", NULL);

  if (port_num < 0 || port_num > 65535)
    return err("verify_port", "Port is out of range");

  if (port_out)
    *port_out = port_num;

  return true;
}
