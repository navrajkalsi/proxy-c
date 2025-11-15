#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "http.h"
#include "main.h"
#include "utils.h"

bool validate_host(const Str *header) {
  if (!header)
    return set_efault();

  // limitations of the check, for now:
  // does not support: use of ip addresses directly

  // tmp null termination
  char *org_ptr = header->data + header->len, org_char = *org_ptr;

  *org_ptr = '\0';
  bool match = exec_regex(&origin_regex, header->data);
  *org_ptr = org_char;

  if (!match)
    return err("exec_regex", NULL);

  // comparing it to the upstream
  // last '/' is optional
  size_t to_compare = *--org_ptr == '/' ? header->len - 1 : header->len;

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

bool set_date_str(Str *date) {
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

bool set_date_string(char *date) {
  if (!date)
    return set_efault();

  time_t now = time(NULL);
  struct tm tm;
  gmtime_r(&now, &tm);

  // strftime returns 0 if write buffer is small
  return (bool)strftime(date, (size_t)DATE_LEN, "%a, %d %b %Y %H:%M:%S GMT",
                        &tm);
}

void set_connection(Connection *conn) {
  if (!conn)
    return;

  if (get_header_value(conn->client_headers.data, "Connection",
                       &conn->connection))
    return;
  else
    warn("get_header_value", "Connection header not found");

  if (equals(conn->http_ver, STR("HTTP/1.0")) ||
      equals(conn->http_ver, STR("HTTP/0.9")))
    conn->connection = STR("close");
  else
    conn->connection = STR("keep-alive");
}

void print_request(const Connection *conn) {
  if (!conn)
    return;

  // just request line
  char *request_line = conn->client_headers.data;
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
  str_print(&conn->host);
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
  case 413:
    return "413 Content Too Large";
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
  case 413:
    return STR("413 Content Too Large");
  case 431:
    return STR("431 Request Header Fields Too Large");
  case 500:
    return STR("500 Internal Server Error");
  case 505:
    return STR("505 HTTP Version Not Supported");
  }

  return STR("500 Internal Server Error");
}
