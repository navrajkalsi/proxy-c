#include <assert.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "connection.h"
#include "main.h"
#include "proxy.h"
#include "utils.h"

bool setup_tls(void)
{
  if (OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL) != 1)
    return err("OPENSSL_init_ssl", NULL);

  const SSL_METHOD *method = TLS_server_method(); // enables TLS support
  ssl_context = SSL_CTX_new(method); // the context stores all certs & keys for the conns

  if (!ssl_context)
  {
    ERR_print_errors_fp(stderr);
    return err("SSL_CTX_new", NULL);
  }

  // setting minimum version for TLS (TLS 1.2)
  if (SSL_CTX_set_min_proto_version(ssl_context, TLS1_2_VERSION) != 1)
  {
    err("SSL_CTX_set_min_proto_version", NULL);
    goto cleanup;
  }

  // using strong cipher suites
  if (SSL_CTX_set_cipher_list(ssl_context, "HIGH:!aNULL:!kRSA:!PSK:!SRP:!MD5:!RC4") != 1)
  {
    err("SSL_CTX_set_cipher_list", NULL);
    goto cleanup;
  }

  if (SSL_CTX_use_certificate_file(ssl_context, DOMAIN_CERT, SSL_FILETYPE_PEM) != 1)
  {
    err("SSL_CTX_use_certificate_file", NULL);
    goto cleanup;
  }

  if (SSL_CTX_use_PrivateKey_file(ssl_context, PRIVATE_KEY, SSL_FILETYPE_PEM) != 1)
  {
    err("SSL_CTX_use_PrivateKey_file", NULL);
    goto cleanup;
  }

  // sanity check, if key & cert match
  if (SSL_CTX_check_private_key(ssl_context) != 1)
  {
    err("SSL_CTX_check_private_key", NULL);
    goto cleanup;
  }

  return true;

cleanup:
  ERR_print_errors_fp(stderr);
  SSL_CTX_free(ssl_context);
  return false;
}

bool setup_proxy(void)
{
  if (config.client_https || config.upstream_https)
    if (!setup_tls())
      return err("setup_tls", NULL);

  // ai_flags=PASSIVE & domain=NULL is required for a socket to be binded
  struct addrinfo hints, *out, *current;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  // null terminated port
  char port_str[config.listen_port.len + 1];
  memcpy(port_str, config.listen_port.data, config.listen_port.len);
  port_str[config.listen_port.len] = '\0';

  int status = 0;
  if ((status = getaddrinfo(config.accept_all ? "::" : "::1", port_str, &hints, &out)) != 0)
    return err("getaddrinfo", gai_strerror(status));

  // getaddrinfo() makes system calls that may sometime set errno, even if getaddrinfo returns 0
  errno = 0;

  for (current = out; current; current = current->ai_next)
  {
    if ((PROXY_FD = socket(current->ai_family, current->ai_socktype, current->ai_protocol)) == -1)
      continue;

    // Have to setsocketopt to allow dual-stack setup supporting both IPv4 & v6
    if (setsockopt(PROXY_FD, IPPROTO_IPV6, IPV6_V6ONLY, &(int){0}, sizeof(int)) == -1)
    {
      close(PROXY_FD);
      PROXY_FD = -2; // setsockopt errors
      continue;
    }

    if (setsockopt(PROXY_FD, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1)
    {
      close(PROXY_FD);
      PROXY_FD = -2;
      continue;
    }

    // Adding timeouts for read and write
    struct timeval time = {.tv_sec = 5, .tv_usec = 0};
    if (setsockopt(PROXY_FD, SOL_SOCKET, SO_RCVTIMEO, &time, sizeof time) == -1)
    {
      close(PROXY_FD);
      PROXY_FD = -2;
      continue;
    }

    if (setsockopt(PROXY_FD, SOL_SOCKET, SO_SNDTIMEO, &time, sizeof time) == -1)
    {
      close(PROXY_FD);
      PROXY_FD = -2;
      continue;
    }

    if (bind(PROXY_FD, current->ai_addr, current->ai_addrlen) == -1)
    {
      close(PROXY_FD);
      PROXY_FD = -3;
      continue;
    }

    // If a valid socket is binded to then break
    break;
  };

  freeaddrinfo(out);
  // dealing with different errors
  if (PROXY_FD < 0)
  {
    if (PROXY_FD == -1)
      return err("socket", strerror(errno));
    else if (PROXY_FD == -2)
      return err("setsockopt", strerror(errno));
    else if (PROXY_FD == -3)
      return err("bind", strerror(errno));
  }

  if (listen(PROXY_FD, BACKLOG) == -1)
    return err("listen", strerror(errno));

  if (!set_non_block(PROXY_FD))
    return err("set_non_block", NULL);

  printf("\nProxy Listening on port: %s\n\n", port_str);

  return true;
}

bool setup_epoll(void)
{
  assert(PROXY_FD >= 0);

  if ((EPOLL_FD = epoll_create(1)) == -1)
    return err("epoll_create", strerror(errno));

  // adding PROXY_FD to epoll as the listening fd
  Connection *conn = NULL;
  if (!(conn = init_conn()))
    return err("init_conn", NULL);

  conn->state = ACCEPT_CLIENT;

  // EPOLLERR & EPOLLHUP do not need to be added manually
  if (!add_to_epoll(conn, PROXY_FD, EPOLLIN | EPOLLERR | EPOLLHUP))
    return err("add_to_epoll", NULL);

  return true;
}

bool start_proxy(void)
{
  timeouts_head = timeouts_tail = NULL;

  int ready_events = -1;
  struct epoll_event epoll_events[MAX_EVENTS]; // this will be filled with the fds that are ready
                                               // with their respective operation type

  while (RUNNING)
  {
    time_t now = time(NULL);
    time_t timeout = timeouts_head ? EXPIRES(timeouts_head) : -1; // for first wait, should be -1

    if ((ready_events = epoll_wait(EPOLL_FD, epoll_events, MAX_EVENTS, (int)timeout * 1000)) == -1)
    {
      if (errno == EINTR && !RUNNING) // ctrl c for example, will not work if
                                      // sighandler is not used first
                                      // otherwise the program just crashes
        break;

      return err("epoll_wait", strerror(errno));
    }

    clear_expired();

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

      else if (conn->state == WRITE_ERROR && events & EPOLLOUT) // write error without upstream
        handle_error_response(conn);

      else if (conn->state == WRITE_REQUEST && events & EPOLLOUT) // send to upstream
        write_request(conn);

      else if (conn->state == READ_RESPONSE && events & EPOLLIN) // read from upstream
        read_response(conn);

      else if (conn->state == WRITE_RESPONSE && events & EPOLLOUT) // send to client
        write_response(conn);

      else if (events & EPOLLHUP)
      {
        warn("check_state", "Hang up detected");
        conn->state = CLOSE_CONN;
      }
      else if (events & EPOLLRDHUP)
      {
        warn("check_state", "Read hang up detected");
        conn->state = CLOSE_CONN;
      }
      else if (events & EPOLLERR)
      {
        warn("check_state", "Error detected on file descriptor");
        conn->state = CLOSE_CONN;
      }
      else
        printf("Unexpected state for epoll_wait(): %s\n", get_state_string(conn->state));

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
  // when handle_state returns, conn.state should be one that start_proxy loop can handle
  switch (conn->state)
  {
  case ACCEPT_CLIENT:
    err("verify_state", "Cannot accept client in handle_state. Logic error");
    break;

  case TLS_CLIENT:
    if (!config.client_https || (config.client_https && setup_endpoint_tls(&conn->client)))
    {
      add_to_epoll(conn, *client_fd, READ_FLAGS);
      conn->state = READ_REQUEST;
    }
    else
    { // for client error cannot send any response, just close
      err("setup_endpoint_tls", NULL);
      conn->state = CLOSE_CONN;
    }
    break;

  case READ_REQUEST:
    mod_in_epoll(conn, *client_fd, READ_FLAGS);
    start_state_timeout(conn, REQUEST_READ);
    break;

  case VERIFY_REQUEST:
    if (*upstream_fd >= 0) // if reusing a upstream from previous res
      conn->state = WRITE_REQUEST;
    else if (verify_request(conn))
      conn->state = CONNECT_UPSTREAM;
    else
      conn->state = WRITE_ERROR;
    print_request(conn);
    goto again;

  case WRITE_ERROR:
    remove_timeout(&conn->conn_timeout);
    remove_timeout(&conn->state_timeout);
    mod_in_epoll(conn, *client_fd, WRITE_FLAGS);
    // do not add timeout here to prevent creating a loop
    break;

  case CONNECT_UPSTREAM:
    if (connect_upstream(upstream_fd))
      conn->state = TLS_UPSTREAM;
    else
    {
      conn->status = 500;
      conn->state = WRITE_ERROR;
    }
    goto again;

  case TLS_UPSTREAM:
    if (!config.upstream_https || (config.upstream_https && setup_endpoint_tls(&conn->upstream)))
    {
      add_to_epoll(conn, *upstream_fd, WRITE_FLAGS);
      conn->state = WRITE_REQUEST;
    }
    else
    { // for upstream error, send error response to client
      err("setup_endpoint_tls", NULL);
      conn->status = 500;
      conn->state = WRITE_ERROR;
    }
    break;

  case WRITE_REQUEST:
    mod_in_epoll(conn, *upstream_fd, WRITE_FLAGS);
    start_state_timeout(conn, REQUEST_WRITE);
    break;

  case READ_RESPONSE:
    mod_in_epoll(conn, *upstream_fd, READ_FLAGS);
    start_state_timeout(conn, RESPONSE_READ);
    break;

  case WRITE_RESPONSE:
    mod_in_epoll(conn, *upstream_fd, WRITE_FLAGS);
    start_state_timeout(conn, RESPONSE_WRITE);
    break;

  case CHECK_CONN:
    check_conn(conn);
    goto again;

  case CLOSE_CONN:
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
