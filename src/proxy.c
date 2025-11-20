#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "client.h"
#include "connection.h"
#include "http.h"
#include "main.h"
#include "proxy.h"
#include "upstream.h"
#include "utils.h"

Connection *active_conns[MAX_CONNECTIONS] = {0};

bool setup_proxy(Config *config, int *proxy_fd)
{
  if (!config || !proxy_fd)
    return set_efault();

  struct addrinfo hints, *out, *current;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE; // for listening sock

  // If getaddrinfo() errors and does not change errno, then have to use gai_strerror()
  int status = 0;
  errno = 0;
  // ai_flags=PASSIVE & domain=NULL is required for a socket to be binded
  if ((status = getaddrinfo(config->accept_all ? "::" : "::1", config->port, &hints, &out)) == -1)
    return err("getaddrinfo", gai_strerror(status));

  errno = 0;

  for (current = out; current; current = current->ai_next)
  {
    if ((*proxy_fd = socket(current->ai_family, current->ai_socktype, current->ai_protocol)) == -1)
      continue;

    // Have to setsocketopt to allow dual-stack setup supporting both IPv4 & v6
    if (setsockopt(*proxy_fd, IPPROTO_IPV6, IPV6_V6ONLY, &(int){0}, sizeof(int)) == -1)
    {
      close(*proxy_fd);
      *proxy_fd = -2; // setsockopt errors
      continue;
    }

    if (setsockopt(*proxy_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1)
    {
      close(*proxy_fd);
      *proxy_fd = -2;
      continue;
    }

    // Adding timeouts for read and write
    struct timeval time = {.tv_sec = 5, .tv_usec = 0};
    if (setsockopt(*proxy_fd, SOL_SOCKET, SO_RCVTIMEO, &time, sizeof time) == -1)
    {
      close(*proxy_fd);
      *proxy_fd = -2;
      continue;
    }

    if (setsockopt(*proxy_fd, SOL_SOCKET, SO_SNDTIMEO, &time, sizeof time) == -1)
    {
      close(*proxy_fd);
      *proxy_fd = -2;
      continue;
    }

    if (bind(*proxy_fd, current->ai_addr, current->ai_addrlen) == -1)
    {
      close(*proxy_fd);
      *proxy_fd = -3;
      continue;
    }

    // If a valid socket is binded to then break, else set proxy_fd to -1
    break;
  };

  freeaddrinfo(out);
  // dealing with different errors
  if (*proxy_fd < 0)
  {
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

bool setup_epoll(int proxy_fd)
{
  EPOLL_FD = epoll_create(1);
  if (EPOLL_FD == -1)
    return err("epoll_create", strerror(errno));

  // adding proxy_fd to epoll as the listening socket
  Connection *conn = NULL;
  if (!(conn = init_conn()))
    return err("init_conn", NULL);

  conn->proxy_fd = proxy_fd;
  conn->state = ACCEPT_CLIENT;

  // EPOLLERR & EPOLLHUP do not need to be added manually
  if (!add_to_epoll(conn, proxy_fd, EPOLLIN | EPOLLERR | EPOLLHUP))
    return err("add_to_epoll", NULL);

  return true;
}

bool start_proxy(void)
{
  int ready_events = -1;
  struct epoll_event epoll_events[MAX_EVENTS]; // this will be filled with the
                                               // fds that are ready with their
                                               // respective operation type

  while (RUNNING)
  {
    if ((ready_events = epoll_wait(EPOLL_FD, epoll_events, MAX_EVENTS, -1)) == -1)
    {
      if (errno == EINTR && !RUNNING) // ctrl c for example, will not work if
                                      // sighandler is not used first
                                      // otherwise the program just crashes
        break;

      return err("epoll_wait", strerror(errno));
    }

    // all subsequent calls should be NON BLOCKING to make epoll make sense
    // all sockets should be set to not block
    // now checking each event and handling it on basis of event specified
    for (int i = 0; i < ready_events; ++i)
    {
      uint32_t events = epoll_events[i].events;
      Connection *conn = epoll_events[i].data.ptr;

      if (conn->state == ACCEPT_CLIENT) // new client
        accept_client(conn->proxy_fd);

      else if (conn->state == READ_REQUEST && events & EPOLLIN) // read from client
        read_request(conn);

      else if (conn->state == WRITE_ERROR &&
               events & EPOLLOUT) // error during read, do not contact upstream
        handle_error_response(conn);

      else if (conn->state == WRITE_REQUEST && events & EPOLLOUT) // send to upstream
        write_request(conn);

      else if (conn->state == READ_RESPONSE && events & EPOLLIN) // read from upstream
        read_response(conn);

      else if (conn->state == WRITE_RESPONSE && events & EPOLLOUT) // send to client
        write_response(conn);

      else if (events & EPOLLHUP)
        puts("hang up");
      else if (events & EPOLLRDHUP)
        puts("read hang up");
      else if (events & EPOLLERR)
        puts("error");
      else
        puts("Unknown state");

      handle_state(conn);
    }
  }

  puts("\nShutting Down...");
  return true;
}

void handle_state(Connection *conn)
{
  if (conn->state == ACCEPT_CLIENT)
    return;

  int *client_fd = &conn->client.fd, *upstream_fd = &conn->upstream.fd;

again:

  log_state(conn->state);

  // when handle_state returns, conn.state should be one that start_proxy loop can handle
  switch (conn->state)
  {
  case ACCEPT_CLIENT:
    err("verify_state", "Cannot accept client in handle_state, logic error");
    break;

  case READ_REQUEST:
    mod_in_epoll(conn, *client_fd, READ_FLAGS);
    break;

  case VERIFY_REQUEST:
    if (verify_request(conn))
      conn->state = CONNECT_UPSTREAM;
    else
      conn->state = WRITE_ERROR;
    print_request(conn);
    goto again;

  case WRITE_ERROR:
    mod_in_epoll(conn, *client_fd, WRITE_FLAGS);
    break;

  case CONNECT_UPSTREAM:
    if (connect_upstream(upstream_fd))
    {
      conn->state = WRITE_REQUEST;
      add_to_epoll(conn, *upstream_fd, WRITE_FLAGS);
    }
    else
    {
      conn->status = 500;
      conn->state = WRITE_ERROR;
    }
    goto again;

  case WRITE_REQUEST:
    mod_in_epoll(conn, *upstream_fd, WRITE_FLAGS);
    break;

  case READ_RESPONSE:
    mod_in_epoll(conn, *upstream_fd, READ_FLAGS);
    break;

  case WRITE_RESPONSE:
    mod_in_epoll(conn, *upstream_fd, WRITE_FLAGS);
    break;

  case RESET_CONN:
    reset_conn(conn);
    break;

  case CLOSE_CONN:
    // TODO: free resources
    if (*client_fd >= 0)
      del_from_epoll(*client_fd);

    if (*upstream_fd >= 0)
      del_from_epoll(*upstream_fd);

    free_conn(&conn);
    break;

  default:
    err("verify_state", "Unknown state");
    break;
  }
}

void free_active_conns(void)
{
  for (int i = 0; i < MAX_CONNECTIONS; ++i)
    if (active_conns[i])
      free_conn(active_conns + i);
}

void log_state(int state)
{
  switch (state)
  {
  case ACCEPT_CLIENT:
    puts("accept conn");
    break;
  case READ_REQUEST:
    puts("read_request");
    break;
  case VERIFY_REQUEST:
    puts("verify_request");
    break;
  case WRITE_ERROR:
    puts("write_error");
    break;
  case CONNECT_UPSTREAM:
    puts("connect_upstream");
    break;
  case WRITE_REQUEST:
    puts("write_request");
    break;
  case READ_RESPONSE:
    puts("read_response");
    break;
  case WRITE_RESPONSE:
    puts("write_response");
    break;
  case RESET_CONN:
    puts("reset_conn");
    break;
  case CLOSE_CONN:
    puts("close_conn");
    break;
  default:
    err("verify_state", "Unknown state");
  }
}
