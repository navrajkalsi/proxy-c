#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "main.h"
#include "proxy.h"
#include "utils.h"

bool setup_proxy(Config *config, int *proxy_fd) {
  if (!config || !proxy_fd)
    return set_efault();

  struct addrinfo hints, *out, *current;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  // If getaddrinfo() errors and does not change errno, then have to use
  // gai_strerror()
  int status = 0;
  errno = 0;
  // ai_flags=PASSIVE & domain=NULL is required for a socket to be binded
  if ((status = getaddrinfo(config->accept_all ? "::" : "::1", config->port,
                            &hints, &out)) == -1) {
    if (!errno)
      return enqueue_error("getaddrinfo", gai_strerror(status));
    else
      return enqueue_error("getaddrinfo", "Getting host info");
  }

  current = out;

  do {
    if ((*proxy_fd =
             socket(out->ai_family, out->ai_socktype, out->ai_protocol)) == -1)
      continue;

    // Have to setsocketopt to allow dual-stack setup supporting both IPv4 & v6
    if (setsockopt(*proxy_fd, IPPROTO_IPV6, IPV6_V6ONLY, &(int){0},
                   sizeof(int)) < 0) {
      *proxy_fd = -2; // setsockopt errors
      goto close;
    }

    if (setsockopt(*proxy_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1},
                   sizeof(int)) < 0) {
      *proxy_fd = -2;
      goto close;
    }

    // Adding timeouts for read and write
    struct timeval time = {.tv_sec = 5, .tv_usec = 0};
    if (setsockopt(*proxy_fd, SOL_SOCKET, SO_RCVTIMEO, &time, sizeof time) <
        0) {
      *proxy_fd = -2;
      goto close;
    }

    if (setsockopt(*proxy_fd, SOL_SOCKET, SO_SNDTIMEO, &time, sizeof time) <
        0) {
      *proxy_fd = -2;
      goto close;
    }

    if (bind(*proxy_fd, current->ai_addr, current->ai_addrlen) < 0) {
      *proxy_fd = -3;
      goto close;
    }

    // If a valid socket is binded to then break, else set proxy_fd to -1
    break;

  close:
    close(*proxy_fd);
    continue;
  } while ((current = current->ai_next));

  freeaddrinfo(out);
  // dealing with different errors
  if (*proxy_fd < -1) {
    if (*proxy_fd == -1)
      return enqueue_error("socket", strerror(errno));
    else if (*proxy_fd == -2)
      return enqueue_error("setsockopt", strerror(errno));
    else if (*proxy_fd == -3)
      return enqueue_error("bind", strerror(errno));
  }

  if (listen(*proxy_fd, BACKLOG) == -1)
    return enqueue_error("listen", strerror(errno));

  printf("\nProxy Listening on port: %s\n\n", config->port);

  return true;
}

bool setup_async(int proxy_fd, int *epoll_fd) {
  if (!epoll_fd)
    return set_efault();

  *epoll_fd = epoll_create1(0);
  if (*epoll_fd == -1)
    return enqueue_error("epoll_create1", strerror(errno));

  // fd set to non blocking
  if (fcntl(*epoll_fd, F_SETFL, O_NONBLOCK) == -1)
    return enqueue_error("fcntl", strerror(errno));

  struct epoll_event event = {.events = EPOLLIN, .data.fd = proxy_fd};

  // adding the entry in the interest list of epoll instance
  // essentially adds proxy_fd to epoll_fd list and the event specifies what to
  // wait for & what fd to do that for
  if (epoll_ctl(*epoll_fd, EPOLL_CTL_ADD, proxy_fd, &event) == -1)
    return enqueue_error("epoll_ctl", strerror(errno));

  return true;
}

bool start_proxy(int epoll_fd) {

  int ready_events = -1;
  // events are epoll_wait() returns from
  struct epoll_event events[MAX_EVENTS];

  while (true)
    if ((ready_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1)) == -1)
      enqueue_error("epoll_wait", strerror(errno));
}
