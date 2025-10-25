#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"
#include "main.h"
#include "poll.h"
#include "utils.h"

bool validate_request(Connection *conn) {
  if (!conn)
    return set_efault();

  Str request = conn->client_request;
  Cut c = cut(request, ' ');

  // verifying method
  if (!c.found) {
    conn->client_status = 400;
    return err("validate_method", "Invalid request");
  } else if (!validate_method(c.head)) {
    conn->client_status = 405;
    return err("validate_method", "Invalid method");
  }

  // finding request path
  c = cut(c.tail, ' ');

  if (!c.found) {
    conn->client_status = 400;
    return err("validate_path", "Invalid request");
  }
  conn->request_path = c.head;

  // finding http version
  c = cut(c.tail, ' ');

  if (!c.found) {
    conn->client_status = 400;
    return err("validate_http_ver", "Invalid request");
  } else if (!validate_http(c.head)) {
    conn->client_status = 500;
    return err("validate_http", "Invalid HTTP version");
  }
  conn->http_ver = c.head;

  // finding the host header
  if (!get_header_value(c.tail.data, "Host", &conn->request_host)) {
    conn->client_status = 400;
    return err("get_header_value", NULL);
  }

  if (!validate_host(&conn->request_host)) {
    conn->client_status = 301;
    return err("validate_host", "Different host in the request header");
  }

  conn->client_status = 200;
  return true;
}

bool validate_host(const Str *header) {
  if (!header)
    return set_efault();

  // limitations of the check, for now:
  // does not support: use of ip addresses directly

  regex_t regex;
  memset(&regex, 0, sizeof regex);
  int status = 0;
  char error_string[256];

  if ((status = regcomp(&regex, ORIGIN_REGEX,
                        REG_EXTENDED | REG_NOSUB | REG_ICASE)) != 0) {
    regerror(status, &regex, error_string, sizeof error_string);
    return err("regcomp", error_string);
  }

  // tmp null termination
  char org_end = header->data[header->len];
  header->data[header->len] = '\0';
  if ((status = regexec(&regex, header->data, 0, NULL, 0)) != 0) {
    regerror(status, &regex, error_string, sizeof error_string);
    regfree(&regex);
    header->data[header->len] = org_end;
    return err("regexec", error_string);
  }

  regfree(&regex);
  header->data[header->len] = org_end;

  // comparing it to the upstream
  size_t to_compare =
      header->data[header->len - 1] == '/' ? header->len - 1 : header->len;

  if (strlen(config.upstream) < to_compare ||
      memcmp(header->data, config.upstream, to_compare) != 0)
    return false;

  return true;
}

bool validate_method(const Str method) { return equals(method, STR("GET")); }

bool validate_http(const Str http_ver) {
  if (equals(http_ver, STR("HTTP/1.0")) || equals(http_ver, STR("HTTP/1.1")) ||
      equals(http_ver, STR("HTTP/2")) || equals(http_ver, STR("HTTP/3")))
    return true;

  return false;
}

bool get_header_value(const char *headers, const char *header_name,
                      Str *header_value) {
  if (!header_name || !headers || !header_value)
    return set_efault();

  char *header_start = NULL;
  char *header_end = NULL;

  // there may be other occurrences of the header name in the header values
  // standard says each header name should follow immediately with a ':'
  // if it does not then finding next match
  while ((header_start = strcasestr(headers, header_name))) {
    // moving request to end of the header name
    headers = header_start += strlen(header_name);
    if (*header_start == ':')
      break;
  }

  if (!header_start)
    return err("strcasestr", "Header name not found");

  // incrementing to start of the header value
  while (isspace(*++header_start))
    ;

  if (!(header_end = strstr(header_start, "\r\n")))
    return err("strstr", "Header end not found");

  header_value->data = header_start;
  header_value->len = header_end - header_start;

  return true;
}

bool set_date(Str *date) {
  if (!date || !date->data)
    return set_efault();

  date->len = DATE_LEN - 1;

  time_t now = time(NULL);
  struct tm tm;
  gmtime_r(&now, &tm);

  // strftime returns 0 if write buffer is small
  return (bool)strftime(date->data, (size_t)DATE_LEN,
                        "%a, %d %b %Y %H:%M:%S GMT", &tm);
}

void print_request(const Connection *conn) {
  if (!conn)
    return;

  // just request line
  char *request_line = conn->client_request.data;
  if (!request_line)
    return;

  char ip_str[INET6_ADDRSTRLEN];
  if (!inet_ntop(AF_INET6,
                 &(((struct sockaddr_in6 *)&conn->client_addr)->sin6_addr),
                 ip_str, sizeof ip_str))
    err("inet_ntop", strerror(errno));
  else
    printf("\n(%s) ", ip_str);

  while (*request_line != '\r' && *request_line != '\n' &&
         *request_line != '\0')
    putchar(*request_line++);

  putchar(' ');

  // host
  str_print(&conn->request_host);
  putchar('\n');
}

char *get_status_string(int status_code) {
  // these strings live for the entire life of the program
  switch (status_code) {
  case 200:
    return "200 OK";
  case 301:
    return "301 Moved Permanently";
  case 400:
    return "400 Bad Request";
  case 403:
    return "403 Forbidden";
  case 404:
    return "404 Not Found";
  case 405:
    return "405 Method Not Allowed";
  case 431:
    return "431 Request Header Fields Too Large";
  case 500:
    return "500 Internal Server Error";
  case 505:
    return "505 HTTP Version Not Supported";
  }

  return "500 Internal Server Error";
}

Str get_status_str(int status_code) {
  // these strings live for the entire life of the program
  switch (status_code) {
  case 200:
    return STR("200 OK");
  case 301:
    return STR("301 Moved Permanently");
  case 400:
    return STR("400 Bad Request");
  case 403:
    return STR("403 Forbidden");
  case 404:
    return STR("404 Not Found");
  case 405:
    return STR("405 Method Not Allowed");
  case 431:
    return STR("431 Request Header Fields Too Large");
  case 500:
    return STR("500 Internal Server Error");
  case 505:
    return STR("505 HTTP Version Not Supported");
  }

  return STR("500 Internal Server Error");
}
