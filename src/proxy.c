#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "client.h"
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
                   sizeof(int)) == -1) {
      *proxy_fd = -2; // setsockopt errors
      goto close;
    }

    if (setsockopt(*proxy_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1},
                   sizeof(int)) == -1) {
      *proxy_fd = -2;
      goto close;
    }

    // Adding timeouts for read and write
    struct timeval time = {.tv_sec = 5, .tv_usec = 0};
    if (setsockopt(*proxy_fd, SOL_SOCKET, SO_RCVTIMEO, &time, sizeof time) ==
        -1) {
      *proxy_fd = -2;
      goto close;
    }

    if (setsockopt(*proxy_fd, SOL_SOCKET, SO_SNDTIMEO, &time, sizeof time) ==
        -1) {
      *proxy_fd = -2;
      goto close;
    }

    if (bind(*proxy_fd, current->ai_addr, current->ai_addrlen) == -1) {
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
  if (*proxy_fd < 0) {
    if (*proxy_fd == -1)
      return enqueue_error("socket", strerror(errno));
    else if (*proxy_fd == -2)
      return enqueue_error("setsockopt", strerror(errno));
    else if (*proxy_fd == -3)
      return enqueue_error("bind", strerror(errno));
  }

  if (listen(*proxy_fd, BACKLOG) == -1)
    return enqueue_error("listen", strerror(errno));

  if (!set_non_block(*proxy_fd))
    return enqueue_error("set_non_block", NULL);

  printf("\nProxy Listening on port: %s\n\n", config->port);

  return true;
}

bool setup_async(int proxy_fd, int *epoll_fd) {
  if (!epoll_fd)
    return set_efault();

  *epoll_fd = epoll_create1(0);
  if (*epoll_fd == -1)
    return enqueue_error("epoll_create1", strerror(errno));

  if (!add_to_epoll(*epoll_fd, proxy_fd, EPOLLIN))
    return enqueue_error("add_to_epoll", NULL);

  return true;
}

bool start_proxy(int proxy_fd, int epoll_fd) {

  int ready_events = -1;
  struct epoll_event
      events[MAX_EVENTS]; // this will be filled with the fds
                          // that are ready with their respective operation type

  while (RUNNING) {
    if ((ready_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1)) == -1) {
      if (errno == EINTR && !RUNNING) // ctrl c for example, will not work if
                                      // sighandler is not used first
                                      // otherwise the program just crashes
        break;

      return enqueue_error("epoll_wait", strerror(errno));
    }

    // all subsequent calls should be NON BLOCKING to make epoll make sense
    // all sockets should be set to not block
    // now checking each event and handling it on basis of event specified
    for (int i = 0; i < ready_events; ++i) {
      if (events[i].data.fd == proxy_fd) { // new client
        if (!accept_client(proxy_fd, epoll_fd))
          err("accept_client", NULL); // do not return
        puts("accepted");
      } else if (events[i].events & EPOLLIN) // read
        puts("Ready to read");
    }
  }

  puts("\nShutting Down\n");
  return true;
}

// after non_block all the system calls on this fd return instantly,
// like read() or write()
// so we can deal with other fds and their events without waiting for this fd to
// finish
bool set_non_block(int fd) {
  if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1)
    return enqueue_error("fcntl", strerror(errno));
  return true;
}

// adds the entry in the interest list of epoll instance
// essentially adds proxy_fd to epoll_fd list and the event specifies what to
// wait for & what fd to do that for
bool add_to_epoll(int epoll_fd, int new_fd, int flags) {
  struct epoll_event event = {.events = flags, .data.fd = new_fd};

  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_fd, &event) == -1)
    return enqueue_error("epoll_ctl", strerror(errno));
  return true;
}
