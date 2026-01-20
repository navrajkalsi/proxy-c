#include <assert.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "client.h"
#include "connection.h"
#include "proxy.h"
#include "timer.h"
#include "upstream.h"
#include "utils.h"

bool setup_tls_ctxs(void)
{
  if (OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL) != 1)
    return err("OPENSSL_init_ssl", NULL);

  // only generate ctx for the side that supposed to be using https

  if (config.client_https && !setup_tls_helper(CLIENT))
    return err("setup_client_tls_context", NULL);

  if (config.upstream_https && !setup_tls_helper(UPSTREAM))
    return err("setup_upstream_tls_context", NULL);

  return true;
}

bool setup_tls_helper(EndpointType type)
{
  assert(type == CLIENT || type == UPSTREAM);

  // proxy acts as a server for client, therefore namings will be inverted
  const SSL_METHOD *method = type == CLIENT ? TLS_server_method() : TLS_client_method();

  SSL_CTX *context = SSL_CTX_new(method); // the context stores all certs & keys for the conns

  if (!context)
  {
    ERR_print_errors_fp(stderr);
    return err("SSL_CTX_new", NULL);
  }

  // setting minimum version for TLS (TLS 1.2)
  if (SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION) != 1)
  {
    err("SSL_CTX_set_min_proto_version", NULL);
    goto cleanup;
  }

  // using strong cipher suites
  if (SSL_CTX_set_cipher_list(context, "HIGH:!aNULL:!kRSA:!PSK:!SRP:!MD5:!RC4") != 1)
  {
    err("SSL_CTX_set_cipher_list", NULL);
    goto cleanup;
  }

  if (type == CLIENT)
  {
    if (SSL_CTX_use_certificate_file(context, DOMAIN_CERT, SSL_FILETYPE_PEM) != 1)
    {
      err("SSL_CTX_use_certificate_file", NULL);
      goto cleanup;
    }

    if (SSL_CTX_use_PrivateKey_file(context, PRIVATE_KEY, SSL_FILETYPE_PEM) != 1)
    {
      err("SSL_CTX_use_PrivateKey_file", NULL);
      goto cleanup;
    }

    // sanity check, if key & cert match
    if (SSL_CTX_check_private_key(context) != 1)
    {
      err("SSL_CTX_check_private_key", NULL);
      goto cleanup;
    }
  }

  if (type == CLIENT)
    client_ssl_ctx = context;
  else
    upstream_ssl_ctx = context;
  return true;

cleanup:
  ERR_print_errors_fp(stderr);
  SSL_CTX_free(context);
  return false;
}

bool setup_proxy(void)
{
  if (config.client_https || config.upstream_https)
    if (!setup_tls_ctxs())
      return err("setup_tls_ctxs", NULL);

  // ai_flags=PASSIVE & domain=NULL is required for a socket to be binded
  struct addrinfo hints, *out, *current;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  // null terminated port
  char port_str[config.listen_port.len + 1];
  memcpy(port_str, config.listen_port.data, (size_t)config.listen_port.len);
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

  set_non_block(PROXY_FD);

  printf("\nProxy Listening on port: %s\n\n", port_str);

  return true;
}

bool setup_epoll(void)
{
  assert(PROXY_FD >= 0);

  if ((EPOLL_FD = epoll_create1(EPOLL_CLOEXEC)) == -1)
    return err("epoll_create", strerror(errno));

  // adding PROXY_FD to epoll as the listening fd
  if (!(PROXY_CONN = init_proxy_conn()))
    return err("init_proxy_conn", NULL);

  // EPOLLERR & EPOLLHUP do not need to be added manually
  add_to_epoll(PROXY_CONN, PROXY_FD, EPOLLIN | EPOLLERR | EPOLLHUP);

  return true;
}

bool start_proxy(void)
{
  int ready_events = -1;
  struct epoll_event epoll_events[MAX_EVENTS]; // this will be filled with the fds that are ready
                                               // with their respective operation type

  while (RUNNING)
  {
    if ((ready_events = epoll_wait(EPOLL_FD, epoll_events, MAX_EVENTS, -1)) == -1)
    {
      if (errno == EINTR && !RUNNING) // running set to false by sig_handler
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
      assert(conn);

      // log_state("start_proxy", conn);
      if (conn->state == ACCEPT_CLIENT) // new client
        accept_client();

      else if (events & EPOLLIN && tfd_expired(conn->conn_tfd))
        conn->state = CONN_TIMEDOUT; // do not need timeout_state, straight 408

      else if (events & EPOLLIN && tfd_expired(conn->state_tfd))
      {
        conn->prev_state = conn->state;
        conn->state = STATE_TIMEDOUT;
      }

      else if (conn->state == PEEK_CLIENT && events & EPOLLIN)
        // no need to drain the peeked data as we are using epolloneshot and it will be rearmed
        peek_client(conn);

      else if (conn->state == READ_REQUEST && events & EPOLLIN) // read from client
        read_request(conn);                                     // write error is not waited on

      else if (conn->state == WRITE_REQUEST && events & EPOLLOUT) // send to upstream
        write_request(conn);

      else if (conn->state == READ_RESPONSE && events & EPOLLIN) // read from upstream
        read_response(conn);

      else if (conn->state == WRITE_RESPONSE && events & EPOLLOUT) // send to client
        write_response(conn);

      else if ((conn->state == SSL_READ && events & EPOLLIN) ||
               (conn->state == SSL_WRITE && events & EPOLLOUT))
      {
        switch (conn->prev_state)
        {
        case TLS_CLIENT:
          conn->state = TLS_CLIENT;
          setup_endpoint_tls(conn, &conn->client);
          break;
        case TLS_UPSTREAM:
          conn->state = TLS_UPSTREAM;
          setup_endpoint_tls(conn, &conn->upstream);
          break;
        case READ_REQUEST:
          conn->state = READ_REQUEST;
          read_request(conn);
          break;
        case WRITE_REQUEST:
          conn->state = WRITE_REQUEST;
          write_request(conn);
          break;
        case READ_RESPONSE:
          conn->state = READ_RESPONSE;
          read_response(conn);
          break;
        case WRITE_RESPONSE:
          conn->state = WRITE_RESPONSE;
          write_response(conn);
          break;
        default:
          printf("Unexpected previous state for %s: %s\n",
                 conn->state == SSL_READ ? "ssl_read" : "ssl_write",
                 get_state_string(conn->prev_state));
          assert(false);
        }
      }

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
      {
        printf("Unexpected state for epoll_wait(): %s\n", get_state_string(conn->state));
        assert(false);
      }

      handle_state(conn);
    }
  }

  puts("\nShutting Down...");
  return true;
}

// all fds will be added to epoll as soon as they are created in the respective function
// only mod fds here
void handle_state(Connection *conn)
{
  if (conn->state == ACCEPT_CLIENT)
    return;

  int *client_fd = &conn->client.fd, *upstream_fd = &conn->upstream.fd;

again:
  if (!RUNNING) // if sigint during loop
    return;

  // log_state("handle_state", conn);
  // when handle_state returns, conn.state should be one that start_proxy loop can handle
  switch (conn->state)
  {
  case ACCEPT_CLIENT:
    err_n_exit("verify_state", "Cannot accept client in handle_state. Logic error");
    break;

  case PEEK_CLIENT:
    err_n_exit("verify_state", "Cannot peek client in handle_state. Logic error");
    break;

  case TLS_CLIENT:
    assert(config.client_https); // only get here if the peeked data is encrypted
    setup_endpoint_tls(conn, &conn->client);
    goto again;

  case READ_REQUEST:
    mod_in_epoll(conn, *client_fd, READ_FLAGS);
    arm_state_tfd(conn->state_tfd, conn->state, 0); // there is no timeout for tls_endpoint
    break;

  case WRITE_ERROR:
    // fire and forget for error message, no need to wait for EPOLL_OUT or timeout
    disarm_tfd(conn->conn_tfd);
    disarm_tfd(conn->state_tfd);
    handle_error_response(conn);
    conn->state = CLOSE_CONN;
    goto again;

  case CONNECT_UPSTREAM:
    connect_upstream(conn);
    goto again;

  case TLS_UPSTREAM:
    setup_endpoint_tls(conn, &conn->upstream);
    goto again;

  case WRITE_REQUEST:
    mod_in_epoll(conn, *upstream_fd, WRITE_FLAGS);
    arm_state_tfd(conn->state_tfd, conn->state, 0);
    break;

  case READ_RESPONSE:
    mod_in_epoll(conn, *upstream_fd, READ_FLAGS);
    arm_state_tfd(conn->state_tfd, conn->state, 0);
    break;

  case WRITE_RESPONSE:
    mod_in_epoll(conn, *client_fd, WRITE_FLAGS);
    arm_state_tfd(conn->state_tfd, conn->state, 0);
    break;

  case CHECK_CONN:
    check_conn(conn);
    goto again;

  case SSL_READ:
    if (conn->prev_state == TLS_CLIENT || conn->prev_state == READ_REQUEST ||
        conn->prev_state == WRITE_RESPONSE)
    {
      mod_in_epoll(conn, *client_fd, READ_FLAGS);
      arm_state_tfd(conn->state_tfd, conn->state, 0);
    }
    else if (conn->prev_state == TLS_UPSTREAM || conn->prev_state == WRITE_REQUEST ||
             conn->prev_state == READ_RESPONSE)
    {
      mod_in_epoll(conn, *upstream_fd, READ_FLAGS);
      arm_state_tfd(conn->state_tfd, conn->state, 0);
    }
    else
    {
      printf("Unexpected previous state for ssl_read: %s\n", get_state_string(conn->prev_state));
      assert(false);
    }
    break;

  case SSL_WRITE:
    if (conn->prev_state == TLS_CLIENT || conn->prev_state == READ_REQUEST ||
        conn->prev_state == WRITE_RESPONSE)
    {
      mod_in_epoll(conn, *client_fd, WRITE_FLAGS);
      arm_state_tfd(conn->state_tfd, conn->state, 0);
    }
    else if (conn->prev_state == TLS_UPSTREAM || conn->prev_state == WRITE_REQUEST ||
             conn->prev_state == READ_RESPONSE)
    {
      mod_in_epoll(conn, *upstream_fd, WRITE_FLAGS);
      arm_state_tfd(conn->state_tfd, conn->state, 0);
    }
    else
    {
      printf("Unexpected previous state for ssl_write: %s\n", get_state_string(conn->prev_state));
      assert(false);
    }
    break;

  case CONN_TIMEDOUT:
    conn->status = 408;
    conn->state = WRITE_ERROR;
    goto again;

  case STATE_TIMEDOUT:
    if (conn->prev_state == SSL_READ || conn->prev_state == SSL_WRITE)
    { // just close conn on ssl timeouts
      conn->state = CLOSE_CONN;
      goto again;
    }
    if (conn->prev_state == READ_REQUEST || conn->prev_state == WRITE_REQUEST)
      conn->status = 408;
    else if (conn->prev_state == READ_RESPONSE || conn->prev_state == WRITE_RESPONSE)
      conn->status = 504;
    else
      assert(false);
    conn->state = WRITE_ERROR;
    goto again;

  case CLOSE_CONN:
    free_conn(&conn);
    break;

  default:
    printf("Unexpected state for handle_state(): %s\n", get_state_string(conn->state));
    assert(false);
  }
}

void free_active_conns(void)
{
  for (int i = 0; i < MAX_CONNECTIONS; ++i)
    if (active_conns[i])
      free_conn(active_conns + i);

  if (PROXY_CONN)
    free(PROXY_CONN);
}

void free_config(void)
{
  if (config.canonical_host.unparsed.len)
    free(config.canonical_host.unparsed.data);
  if (config.upstream_host.unparsed.len)
    free(config.upstream_host.unparsed.data);
  if (config.listen_port.len)
    free(config.listen_port.data);
}
