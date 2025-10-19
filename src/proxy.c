#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "client.h"
#include "main.h"
#include "poll.h"
#include "proxy.h"
#include "request.h"
#include "utils.h"

// possible ways to connect to upstream
struct addrinfo *upstream_addrinfo = NULL;

bool setup_proxy(Config *config, int *proxy_fd) {
  if (!config || !proxy_fd)
    return set_efault();

  struct addrinfo hints, *out, *current;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE; // for listening sock

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
    if ((*proxy_fd = socket(current->ai_family, current->ai_socktype,
                            current->ai_protocol)) == -1)
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

bool setup_epoll(int proxy_fd, int *epoll_fd) {
  if (!epoll_fd)
    return set_efault();

  *epoll_fd = epoll_create1(0);
  if (*epoll_fd == -1)
    return enqueue_error("epoll_create1", strerror(errno));

  EventData *data = NULL;
  if (!(data = init_event_data(TYPE_FD, (epoll_data_t)proxy_fd)))
    return enqueue_error("init_event_data", NULL);

  if (!add_to_epoll(*epoll_fd, data, EPOLLIN))
    return enqueue_error("add_to_epoll", NULL);

  return true;
}

bool setup_upstream(const char *upstream) {
  if (!upstream)
    return set_efault();

  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;

  int status = errno = 0;
  // targetting port based on the protocol of the upstream
  if ((status =
           getaddrinfo(upstream, !memcmp("https", upstream, 5) ? "443" : "80",
                       &hints, &upstream_addrinfo)) == -1) {
    if (!errno)
      return enqueue_error("getaddrinfo", gai_strerror(status));
    else
      return enqueue_error("getaddrinfo", "Getting host info");
  }

  return true;
}

bool connect_upstream(int *upstream_fd) {
  struct addrinfo *current = upstream_addrinfo;
  if (!current)
    return err("verify_upstream", "Upstream address info is NULL");

  do {
    if ((*upstream_fd = socket(current->ai_family, current->ai_socktype,
                               current->ai_protocol)) == -1)
      continue;

    // Have to setsocketopt to allow dual-stack setup supporting both IPv4 & v6
    if (setsockopt(*upstream_fd, IPPROTO_IPV6, IPV6_V6ONLY, &(int){0},
                   sizeof(int)) == -1) {
      *upstream_fd = -2; // setsockopt errors
      goto close;
    }

    if (connect(*upstream_fd, current->ai_addr, current->ai_addrlen) == -1) {
      *upstream_fd = -3;
      goto close;
    }

    // If a valid socket is connected to then break
    break;

  close:
    close(*upstream_fd);
    continue;
  } while ((current = current->ai_next));

  // dealing with different errors
  if (*upstream_fd < 0) {
    if (*upstream_fd == -1)
      return err("socket", strerror(errno));
    else if (*upstream_fd == -2)
      return err("setsockopt", strerror(errno));
    else if (*upstream_fd == -3)
      return err("connect", strerror(errno));
  }

  if (!set_non_block(*upstream_fd))
    return err("set_non_block", NULL);

  return true;
}

bool start_proxy(int epoll_fd) {
  int ready_events = -1;
  struct epoll_event events[MAX_EVENTS]; // this will be filled with the fds
                                         // that are ready with their
                                         // respective operation type

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
      struct epoll_event event = events[i];
      EventData *event_data = (EventData *)event.data.ptr;

      if (event_data->data_type == TYPE_FD) { // new client
        if (!accept_client(event_data->data.fd, epoll_fd))
          err("accept_client", NULL); // do not return
        puts("accepted");
      } else if (event_data->data_type == TYPE_PTR_CLIENT &&
                 event.events & EPOLLIN) { // read from client
        puts("Ready to read from client");
        if (!handle_request(event_data))
          err("handle_request", strerror(errno));
      } else if (event_data->data_type == TYPE_PTR_UPSTREAM &&
                 event.events & EPOLLIN) // read from upstream
        puts("Ready to read from server");
      else if (event_data->data_type == TYPE_PTR_CLIENT &&
               event.events & EPOLLOUT) // send to client
        puts("Ready to send to client");
      else if (event_data->data_type == TYPE_PTR_UPSTREAM &&
               event.events & EPOLLOUT) // send to upstream
        puts("Ready to send to upstream");
      else
        err("verify_vaildate_data", "Unknown event data");
    }
  }

  puts("\nShutting Down\n");
  return true;
}

void free_upstream_addrinfo(void) {
  if (upstream_addrinfo)
    freeaddrinfo(upstream_addrinfo);
}
