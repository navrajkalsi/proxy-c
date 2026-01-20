#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sched.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "connection.h"
#include "proxy.h"
#include "str.h"
#include "timer.h"
#include "utils.h"

Connection *active_conns[MAX_CONNECTIONS] = {0};
int active_conns_num = 0;

Connection *init_conn(void)
{
  Connection *conn;
  if (!(conn = calloc(1, sizeof(Connection))))
    return err_null("calloc", strerror(errno));

  if (!activate_conn(conn))
  {
    free(conn);
    return err_null("activate_conn", "Max limit of active connections reached");
  }

  Endpoint *client = &conn->client, *upstream = &conn->upstream;

  client->fd = upstream->fd = -1;
  client->next_index = upstream->next_index = 0;

  client->head.data = client->buffer; // initally request points to beginning of the buffer
  client->type = CLIENT;
  upstream->head.data = upstream->buffer;
  upstream->type = UPSTREAM;

  conn->conn_tfd = conn->state_tfd = -1;
  create_tfd(&conn->conn_tfd);
  create_tfd(&conn->state_tfd);

  // no need to mod, flags never change, just arm or disarm with helper funcs
  add_to_epoll(conn, conn->conn_tfd, TIMER_FLAGS);
  add_to_epoll(conn, conn->state_tfd, TIMER_FLAGS);

  reset_conn(conn);

  return conn;
}

Connection *init_proxy_conn(void)
{ // only for PROXY_FD
  Connection *conn;
  if (!(conn = calloc(1, sizeof(Connection))))
    return err_null("calloc", strerror(errno));

  conn->state = ACCEPT_CLIENT;

  return conn;
}

void free_conn(Connection **conn)
{
  assert(conn && *conn);

  Connection *to_free = *conn;
  deactivate_conn(*conn);

  if (to_free->client.fd >= 0)
  {
    del_from_epoll(to_free->client.fd);
    close(to_free->client.fd);
  }

  if (to_free->upstream.fd >= 0)
  {
    del_from_epoll(to_free->upstream.fd);
    close(to_free->upstream.fd);
  }

  del_from_epoll(to_free->conn_tfd);
  del_from_epoll(to_free->state_tfd);

  close(to_free->conn_tfd);
  close(to_free->state_tfd);

  if (to_free->client.ssl)
  {
    SSL_shutdown(to_free->client.ssl);
    SSL_free(to_free->client.ssl);
    to_free->client.ssl = NULL;
  }

  if (to_free->upstream.ssl)
  {
    SSL_shutdown(to_free->upstream.ssl);
    SSL_free(to_free->upstream.ssl);
    to_free->upstream.ssl = NULL;
  }

  free(to_free);
  to_free = NULL;
}

bool activate_conn(Connection *conn)
{
  assert(conn);

  for (int i = 0; i < MAX_CONNECTIONS; ++i)
    if (!active_conns[i])
    {
      active_conns[i] = conn;
      conn->self_ptr = active_conns + i;

      ++active_conns_num;

      return true;
    }

  return false;
}

void deactivate_conn(Connection *conn)
{
  assert(conn);

  *(conn->self_ptr) = NULL;
  conn->self_ptr = NULL;

  --active_conns_num;
}

void reset_conn(Connection *conn)
{
  assert(conn);

  conn->state = READ_REQUEST;

  Endpoint *client = &conn->client, *upstream = &conn->upstream;

  pull_buf(client);
  pull_buf(upstream);

  client->head.len = upstream->head.len = 0;
  client->read_index = upstream->read_index = 0;
  client->write_index = upstream->write_index = 0;
  client->to_read = upstream->to_read = BUFFER_SIZE;
  client->to_write = upstream->to_write = 0;
  client->content_len = upstream->content_len = 0;
  client->chunked = upstream->chunked = false;
  client->headers_found = upstream->headers_found = false;

  reset_chunk_tracker(&client->chunk_tracker);
  reset_chunk_tracker(&upstream->chunk_tracker);

  client->headers.host = upstream->headers.host = NULL_STR;
  client->headers.connection = upstream->headers.connection = NULL_STR;
  client->headers.content_length = upstream->headers.content_length = NULL_STR;
  client->headers.transfer_encoding = upstream->headers.transfer_encoding = NULL_STR;

  conn->status = 0;
  conn->protocol = NULL_STR;
  conn->target = NULL_STR;
  conn->keep_alive = false;
  conn->complete = false;

  arm_conn_tfd(conn->conn_tfd, 0);
}

// after non_block all the system calls on this fd return instantly,
// like read() or write(). so we can deal with other fds and their
// events without waiting for this fd to finish
void set_non_block(int fd)
{
  int flags = fcntl(fd, F_GETFL, 0);
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    err_n_exit("fcntl", strerror(errno));
}

// adds the entry in the interest list of epoll instance
// essentially adds fd to epoll_fd list and the event specifies what to
// wait for & what fd to do that for
void add_to_epoll(Connection *conn, int fd, int flags)
{
  assert(fd >= 0);

  // this struct does not need to be on the heap
  // kernel copies all the data into the epoll table
  struct epoll_event epoll_event = {.events = (uint)flags, .data.ptr = (void *)conn};

  if (epoll_ctl(EPOLL_FD, EPOLL_CTL_ADD, fd, &epoll_event) == -1)
    err_n_exit("epoll_ctl_add", strerror(errno));
}

void mod_in_epoll(Connection *conn, int fd, int flags)
{
  assert(fd >= 0);

  struct epoll_event epoll_event = {.events = (uint)flags, .data.ptr = (void *)conn};

  if (epoll_ctl(EPOLL_FD, EPOLL_CTL_MOD, fd, &epoll_event) == -1)
    err_n_exit("epoll_ctl_mod", strerror(errno));
}

void del_from_epoll(int fd)
{
  assert(fd >= 0);

  if (epoll_ctl(EPOLL_FD, EPOLL_CTL_DEL, fd, NULL) == -1)
    err_n_exit("epoll_ctl_del", strerror(errno));
}

void pull_buf(Endpoint *endpoint)
{
  assert(endpoint);

  if (!endpoint->next_index)
    return;

  assert(endpoint->read_index > endpoint->next_index);

  size_t to_copy = (size_t)(endpoint->read_index - endpoint->next_index);
  memcpy(endpoint->buffer, endpoint->buffer + endpoint->next_index, to_copy);
  endpoint->read_index = (ptrdiff_t)to_copy;
  endpoint->to_read = BUFFER_SIZE - (size_t)endpoint->read_index - 1;
  endpoint->next_index = 0;

  endpoint->headers_found = false;
}

void check_conn(Connection *conn)
{
  assert(conn);
  assert(conn->state == CHECK_CONN);
  assert(conn->complete);

  if (conn->keep_alive)
    reset_conn(conn); // start to read again from client (no need to tls again)
  else
    conn->state = CLOSE_CONN;
}

void print_endpoint(const Endpoint *endpoint)
{
  if (!endpoint)
    return;

  puts("\033[1;34mDebug info:\n");
  printf("\033[1;33mBuffer:\033[0;32m %.*s\n", (int)endpoint->read_index, endpoint->buffer);
  printf("\033[1;33mHead:\033[0;32m %.*s\n", (int)endpoint->head.len, endpoint->head.data);
  printf("\033[1;33mHead len:\033[0;32m %ld\n", endpoint->head.len);
  printf("\033[1;33mRead index:\033[0;32m %ld\n", endpoint->read_index);
  printf("\033[1;33mWrite index:\033[0;32m %ld\n", endpoint->write_index);
  printf("\033[1;33mNext index:\033[0;32m %ld\n", endpoint->next_index);
  printf("\033[1;33mTo read:\033[0;32m %zu\n", endpoint->to_read);
  printf("\033[1;33mTo write:\033[0;32m %zu\n", endpoint->to_write);
  printf("\033[1;33mContent len:\033[0;32m %zu\n", endpoint->content_len);
  printf("\033[1;33mChunked:\033[0;32m %s\n", endpoint->chunked ? "true" : "false");
  printf("\033[1;33mHeaders found:\033[0;32m %s\n", endpoint->headers_found ? "true" : "false");
  printf("\033[1;33mChunk Tracker buffer:\033[0;32m %.*s\n", (int)endpoint->chunk_tracker.view_len,
         endpoint->chunk_tracker.buffer);
  puts("\033[1;34mEnd\n\033[0m");
}

void setup_endpoint_tls(Connection *conn, Endpoint *endpoint)
{
  assert(conn && endpoint);
  assert(conn->state == TLS_CLIENT || conn->state == TLS_UPSTREAM);
  if (config.client_https)
    assert(client_ssl_ctx);
  if (config.upstream_https)
    assert(upstream_ssl_ctx);

  bool client = &conn->client == endpoint, upstream = &conn->upstream == endpoint;
  assert(client || upstream);
  if (client)
    assert(config.client_https);
  if (upstream)
    assert(config.upstream_https);

  SSL_CTX *context = client ? client_ssl_ctx : upstream_ssl_ctx;

  if (!endpoint->ssl)
  { // this may be the second time this function is being called after SSL_READ or WRITE states
    if (!(endpoint->ssl = SSL_new(context)))
    {
      err("SSL_new", NULL);
      goto error;
    }

    if (!SSL_set_fd(endpoint->ssl, endpoint->fd))
    {
      err("SSL_set_fd", NULL);
      goto error;
    }

    if (client)
      SSL_set_accept_state(endpoint->ssl);
    else
      SSL_set_connect_state(endpoint->ssl);
  }

  int ret = SSL_do_handshake(endpoint->ssl);

  if (ret == 1)
  {
    conn->state = client ? READ_REQUEST : WRITE_REQUEST;
    return;
  }

  int ssl_err = SSL_get_error(endpoint->ssl, ret);
  // log_ssl_error(ssl_err);
  switch (ssl_err)
  {
  case SSL_ERROR_WANT_READ: // wait for pollin
    conn->prev_state = conn->state;
    conn->state = SSL_READ;
    return;
  case SSL_ERROR_WANT_WRITE: // wait for pollout
    conn->prev_state = conn->state;
    conn->state = SSL_WRITE;
    return;
  case SSL_ERROR_SSL: // fatal error, close right away
    conn->state = CLOSE_CONN;
    return;
  default:
    goto error;
  }

error:
  ERR_print_errors_fp(stderr);
  ERR_clear_error();
  conn->state = client ? CLOSE_CONN : WRITE_ERROR;
  conn->status = 500;
  return;
}

Str get_redirect_location(Connection *conn)
{ // client buffer's data would be useless at this point, except the path inside the request line
  assert(conn);
  assert(conn->target.len &&
         *conn->target.data == '/'); // other forms were rejected during parse_request_line
  // at this point, target will not be larger MAX_REQUEST_TARGET, ie, half of BUFFER_SIZE
  // so the location will definitely fit inside BUFFER_SIZE of client buffer

  // target is inside the client buffer, localize it and then alter client buffer
  char local[conn->target.len];
  Str target_local = {.data = local, .len = conn->target.len};
  memcpy(target_local.data, conn->target.data, (size_t)target_local.len);

  Str location[] = {config.client_https ? HTTPS : HTTP, STR("://"), config.canonical_host.unparsed,
                    target_local}, // path must be in origin form(begins with /)
      ret = {.data = conn->client.buffer, .len = 0};
  uint num = sizeof location / sizeof(Str);

  for (uint i = 0; i < num; i++)
  {
    // only path can cause overflow, but it would have been rejected during parse_request_line
    assert(ret.len + location[i].len <= (ptrdiff_t)BUFFER_SIZE);
    memcpy(ret.data + ret.len, location[i].data, (size_t)location[i].len);
    ret.len += location[i].len;
  }

  return ret;
}
