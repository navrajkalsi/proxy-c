#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <time.h>
#include <unistd.h>

#include "client.h"
#include "event.h"
#include "main.h"
#include "proxy.h"
#include "utils.h"

// global var that stores a linked list of struct addrinfo containing info about
// upstream server
struct addrinfo *upstream_addrinfo = NULL;
Connection *active_conns[MAX_CONNECTIONS] = {0};

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
                            &hints, &out)) == -1)
    return err("getaddrinfo", gai_strerror(status));

  errno = 0;

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
      return err("socket", strerror(errno));
    else if (*proxy_fd == -2)
      return err("setsockopt", strerror(errno));
    else if (*proxy_fd == -3)
      return err("bind", strerror(errno));
  }

  if (listen(*proxy_fd, BACKLOG) == -1)
    return err("listen", strerror(errno));

  if (!set_non_block(*proxy_fd))
    return err("set_non_block", NULL);

  printf("\nProxy Listening on port: %s\n\n", config->port);

  return true;
}

bool setup_epoll(int proxy_fd) {
  EPOLL_FD = epoll_create(1);
  if (EPOLL_FD == -1)
    return err("epoll_create", strerror(errno));

  // adding proxy_fd to epoll as the listening socket
  Connection *conn = NULL;
  if (!(conn = init_connection()))
    return err("init_connection", NULL);

  conn->proxy_fd = proxy_fd;
  conn->state = ACCEPT_CONN;

  // EPOLLERR & EPOLLHUP do not need to be added manually
  if (!add_to_epoll(conn, proxy_fd, EPOLLIN | EPOLLERR | EPOLLHUP))
    return err("add_to_epoll", NULL);

  return true;
}

bool setup_upstream(char *upstream) {
  if (!upstream)
    return set_efault();

  // removing / at the end, if found
  size_t len = strlen(upstream);
  while (upstream[--len] == '/')
    upstream[len] = '\0';

  // upstream may contain port at the end, parsing it
  // else using if any http protocol is specificed
  // otherwise using FALLBACK_UPSTREAM_PORT
  char *colon = upstream, *port = NULL, *tmp = NULL, *revert = NULL;
  while ((tmp = strchr(colon, ':')))
    colon = ++tmp;

  if (colon == upstream) // no : found
    port = FALLBACK_UPSTREAM_PORT;
  else {
    --colon; // now colon is at ':', makes reasoning a little easier
    if (colon[1] == '/') {  // the colon is followed after http or https
      if (colon[-1] == 's') // https
        port = "443";
      else // http
        port = "80";
    } else { // the colon is followed by the port at the end
      *colon =
          '\0'; // now the port and origin are separated by a null terminator
      revert = colon; // need to revert it back to the original format to be
                      // able to compare to the host header of the request
      port = ++colon;
    }
  }

  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;

  int status = 0;
  // targetting port based on the protocol of the upstream
  if ((status = getaddrinfo(upstream, port, &hints, &upstream_addrinfo)) != 0)
    return err("getaddrinfo", gai_strerror(status));

  // getaddrinfo() makes system calls that may sometime set errno, even if the
  // getaddrinfo returns 0
  errno = 0;

  if (revert)
    *revert = ':';

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

    // Have to setsocketopt to allow dual-stack setup supporting both IPv4 &
    // v6
    if (setsockopt(*upstream_fd, IPPROTO_IPV6, IPV6_V6ONLY, &(int){0},
                   sizeof(int)) == -1) {
      *upstream_fd = -2;
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

bool start_proxy(void) {
  int ready_events = -1;
  struct epoll_event epoll_events[MAX_EVENTS]; // this will be filled with the
                                               // fds that are ready with their
                                               // respective operation type

  while (RUNNING) {
    if ((ready_events = epoll_wait(EPOLL_FD, epoll_events, MAX_EVENTS, -1)) ==
        -1) {
      if (errno == EINTR && !RUNNING) // ctrl c for example, will not work if
                                      // sighandler is not used first
                                      // otherwise the program just crashes
        break;

      return err("epoll_wait", strerror(errno));
    }

    // all subsequent calls should be NON BLOCKING to make epoll make sense
    // all sockets should be set to not block
    // now checking each event and handling it on basis of event specified
    for (int i = 0; i < ready_events; ++i) {
      uint32_t events = epoll_events[i].events;
      Connection *conn = epoll_events[i].data.ptr;

      if (conn->state == ACCEPT_CONN) // new client
        accept_client(conn->proxy_fd);

      else if (conn->state == READ_REQUEST &&
               events & EPOLLIN) // read from client
        read_client(conn);

      else if (conn->state == READ_RESPONSE &&
               events & EPOLLIN) // read from upstream
        puts("Ready to read from server");

      else if (conn->state == WRITE_RESPONSE &&
               events & EPOLLOUT) { // send to client
        if (conn->client_status &&
            conn->client_status != 200) { // error during read, did not contact
                                          // upstream
          if (!handle_error_response(conn))
            err("handle_error_response", NULL);
          // timeout
          continue;
        } else
          puts("Ready to send to client");
      }

      else if (conn->state == WRITE_REQUEST &&
               events & EPOLLOUT) // send to upstream
        puts("Ready to send to upstream");

      else if (events & EPOLLHUP)
        puts("hang up");
      else if (events & EPOLLRDHUP)
        puts("read hang up");
      else if (events & EPOLLERR)
        puts("error");

      else
        err("verify_vaildate_data", "Unknown event data");

      handle_state(conn);
    }
  }

  puts("\nShutting Down...");
  free_upstream_addrinfo();
  free_active_conns();

  return true;
}

void handle_state(Connection *conn) {
  if (conn->state == ACCEPT_CONN)
    return;

  switch (conn->state) {
  case READ_REQUEST:
    puts("read_request");
    break;
  case VERIFY_REQUEST:
    puts("verify_request");
    break;
  case WRITE_ERROR:
    puts("write_error");
    break;
  case WRITE_RESPONSE:
    puts("write_response");
    break;
  case READ_RESPONSE:
    puts("read_response");
    break;
  case WRITE_REQUEST:
    puts("write_request");
    break;
  case CLOSE_CONN:
    puts("close_conn");
    break;
  case ACCEPT_CONN:
    puts("accept conn");
    break;
  }

  // verify_request may change the state, so it goes first
  if (conn->state == VERIFY_REQUEST)
    verify_request(conn);

  // CHANGE ALL EVENT TO CONNS

  if (conn->state == CLOSE_CONN) // client disconnect or something else
                                 // TODO: free resources
    del_from_epoll(event);

  if (conn->state == READ_REQUEST) // read more
    mod_in_epoll(event, READ_FLAGS);
  else if (conn->state ==
           WRITE_ERROR) // error from reading request or verifying request
    mod_in_epoll(event, WRITE_FLAGS);
  else if (conn->state == WRITE_RESPONSE) // send error response
    mod_in_epoll(event, WRITE_FLAGS);
  else
    err("handle_state", "Unknown state for client, logic error");

  if (conn->state == WRITE_REQUEST) // contact upstream
    mod_in_epoll(event, WRITE_FLAGS);
}

void free_upstream_addrinfo(void) {
  if (upstream_addrinfo)
    freeaddrinfo(upstream_addrinfo);
}

void free_active_conns(void) {
  for (int i = 0; i < MAX_CONNECTIONS; ++i)
    if (active_events[i])
      free_event_conn(active_events + i);
}
