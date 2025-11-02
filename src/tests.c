#include "tests.h"
#include <stdio.h>

void verify_read_client(void) {
  char *no_body = "GET / HTTP/1.1\r\n"
                  "Host: domain.com\r\n"
                  "Content-Length: 0\r\n"
                  "\r\n";

  puts(no_body);

  return;
}
