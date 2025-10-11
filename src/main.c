#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <main.h>
#include <utils.h>

int main(void) {
  struct addrinfo hints, *out;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  (void)getaddrinfo("::1", "0", &hints, &out);

  int server_fd = socket(out->ai_family, out->ai_socktype, out->ai_protocol);

  enqueue_error("socket", strerror(errno));

  print_error_list();
  free_error_list();

  printf("socket: %d\n", server_fd);

  printf("port: %d\n", ntohs(((struct sockaddr_in6 *)out->ai_addr)->sin6_port));

  (void)bind(server_fd, out->ai_addr, out->ai_addrlen);
  freeaddrinfo(out);

  listen(server_fd, 25);

  int client_fd = accept(server_fd, NULL, NULL);
  printf("client: %d\n", client_fd);

  char buf[4096];

  int red = read(client_fd, buf, 4096);
  printf("response:\n%.*s\n", red, buf);

  write(client_fd, "<h1>hello</h1>", strlen("<h1>hello</h1>"));

  close(client_fd);

  return 0;
}
