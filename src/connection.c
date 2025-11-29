#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <time.h>
#include <unistd.h>

#include "connection.h"
#include "http.h"
#include "main.h"
#include "proxy.h"
#include "timeout.h"
#include "utils.h"

Connection *active_conns[MAX_CONNECTIONS] = {0};
int active_conns_num = 0;

Connection *init_conn(void)
{
  Connection *conn;
  if (!(conn = malloc(sizeof(Connection))))
  {
    err("malloc", strerror(errno));
    return NULL;
  }

  if (!activate_conn(conn))
  {
    err("activate_conn", errno ? strerror(errno) : "Max limit of active connections reached");
    free(conn);
    return NULL;
  }

  memset(&conn->client_addr, 0, sizeof(struct sockaddr_storage));

  Endpoint *client = &conn->client, *upstream = &conn->upstream;

  // same vars across client and upstream
  client->fd = upstream->fd = -1;
  client->next_index = upstream->next_index = 0;

  client->headers.data = client->buffer; // initally request points to beginning of the buffer

  upstream->headers.data = upstream->buffer;

  reset_conn(conn);

  return conn;
}

void free_conn(Connection **conn)
{
  if (!conn || !*conn)
    return;

  Connection *to_free = *conn;
  deactivate_conn(*conn);

  remove_timeout(&to_free->conn_timeout);
  remove_timeout(&to_free->state_timeout);

  free(to_free);
  to_free = NULL;
}

bool activate_conn(Connection *conn)
{
  if (!conn)
    return set_efault();

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
  if (!conn)
    return;

  *(conn->self_ptr) = NULL;
  conn->self_ptr = NULL;

  --active_conns_num;
}

void reset_conn(Connection *conn)
{
  if (!conn)
    return;

  conn->state = READ_REQUEST;

  Endpoint *client = &conn->client, *upstream = &conn->upstream;

  pull_buf(client);
  pull_buf(upstream);

  client->headers.len = upstream->headers.len = 0;
  client->read_index = upstream->read_index = 0;
  client->write_index = upstream->write_index = 0;
  client->to_read = upstream->to_read = BUFFER_SIZE - 1;
  client->to_write = upstream->to_write = 0;
  client->content_len = upstream->content_len = 0;
  client->chunked = upstream->chunked = false;
  client->headers_found = upstream->headers_found = false;
  *client->last_chunk_found = *upstream->last_chunk_found = '\0';

  conn->status = 0;
  conn->proxy_fd = -1;
  conn->http_ver = STR(FALLBACK_HTTP_VER);
  conn->host = ERR_STR;
  conn->path = ERR_STR;
  conn->keep_alive = false;
  conn->complete = false;

  // only conn_timeout is started, state timeout is not touched
  start_conn_timeout(conn, -1);
}

// after non_block all the system calls on this fd return instantly,
// like read() or write(). so we can deal with other fds and their
// events without waiting for this fd to finish
bool set_non_block(int fd)
{
  int flags = fcntl(fd, F_GETFL, 0);
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    return err("fcntl", strerror(errno));
  return true;
}

// adds the entry in the interest list of epoll instance
// essentially adds fd to epoll_fd list and the event specifies what to
// wait for & what fd to do that for
bool add_to_epoll(Connection *conn, int fd, int flags)
{
  // this struct does not need to be on the heap
  // kernel copies all the data into the epoll table
  struct epoll_event epoll_event = {.events = (uint)flags, .data.ptr = (void *)conn};

  if (fd == -1)
    return err("get_target_fd",
               "Socket is probably being added to epoll before accepting/connecting");

  if (epoll_ctl(EPOLL_FD, EPOLL_CTL_ADD, fd, &epoll_event) == -1)
    return err("epoll_ctl_add", strerror(errno));

  return true;
}

bool mod_in_epoll(Connection *conn, int fd, int flags)
{
  struct epoll_event epoll_event = {.events = (uint)flags, .data.ptr = (void *)conn};

  if (fd == -1)
    return err("get_target_fd", "Socket fd is not initialized. Logic error!");

  if (epoll_ctl(EPOLL_FD, EPOLL_CTL_MOD, fd, &epoll_event) == -1)
    return err("epoll_ctl_mod", strerror(errno));

  return true;
}

bool del_from_epoll(int fd)
{
  if (fd == -1)
    return err("get_target_fd", "Socket fd is not initialized");

  if (epoll_ctl(EPOLL_FD, EPOLL_CTL_DEL, fd, NULL) == -1)
    return err("epoll_ctl_del", strerror(errno));

  return true;
}

void pull_buf(Endpoint *endpoint)
{
  if (!endpoint || !endpoint->next_index)
    return;

  assert(endpoint->read_index > endpoint->next_index);

  size_t to_copy = (size_t)(endpoint->read_index - endpoint->next_index);
  memcpy(endpoint->buffer, endpoint->buffer + endpoint->next_index, to_copy);
  endpoint->read_index = (ptrdiff_t)to_copy;
  endpoint->to_read = BUFFER_SIZE - (size_t)endpoint->read_index - 1;
  endpoint->next_index = 0;

  endpoint->headers_found = false;
}

bool find_last_chunk(Endpoint *endpoint)
{
  if (!endpoint)
    return set_efault();

  assert(endpoint->headers_found);

  // pointer at chars after headers
  char *start = endpoint->buffer + endpoint->headers.len;

  if (!*start) // no body
    return false;

  // first checking if already reading last chunk from previous call
  size_t matched = strlen(endpoint->last_chunk_found);

  while (matched && matched < (size_t)LAST_CHUNK_STR.len && *start)
  { // continue to check for last_chunk
    if (*start != LAST_CHUNK[matched])
    { // mismatch
      *endpoint->last_chunk_found = '\0';
      matched = 0;
      break;
    }

    endpoint->last_chunk_found[matched] = LAST_CHUNK[matched];
    endpoint->last_chunk_found[++matched] = '\0';

    start++;
  }

  if (matched == (size_t)LAST_CHUNK_STR.len)
  {             // full chunk matched
    if (*start) // next request
      endpoint->next_index = start - endpoint->buffer;
    return true;
  }
  else if (matched) // full chunk not matched but ran out of chars
    return false;   // read more

  // nothing more to compare
  if (!*start)
    return false;

  // last chunk not found in the beginning
  // now searching beyond
  char *last_chunk = LAST_CHUNK;
  if ((last_chunk = strstr(start, last_chunk)))
  { // last chunk was read
    ptrdiff_t chunk_end =
        (last_chunk + LAST_CHUNK_STR.len) - endpoint->buffer; // this is past the \n

    if (endpoint->buffer[chunk_end]) // read the body and another request
      endpoint->next_index = chunk_end;

    // read the body and nothing more
    return true;
  }

  // full last chunk not found, check last bytes in the buffer for worst case:
  // '0\r\n\r'
  start = endpoint->buffer + endpoint->headers.len;
  size_t read_size = strlen(start), // num of chars available to check at most
      to_match =
          read_size < (size_t)LAST_CHUNK_STR.len - 1 ? read_size : (size_t)LAST_CHUNK_STR.len - 1;
  ptrdiff_t match_index = (ptrdiff_t)(read_size - to_match);
  matched = 0;

  // checking if 0 is received, only using last to_match bytes
  while (start[match_index])
  {
    if (start[match_index] != LAST_CHUNK[matched])
    {
      matched = 0;
      *endpoint->last_chunk_found = '\0'; // restart
    }
    else
    {
      endpoint->last_chunk_found[matched] = LAST_CHUNK[matched];
      endpoint->last_chunk_found[++matched] = '\0';
    }

    match_index++;
  }

  return false;
}

bool parse_headers(Connection *conn, Endpoint *endpoint)
{
  if (!conn || !endpoint)
    return set_efault();

  // this function should not be used after the headers are read
  assert(!endpoint->headers_found);
  assert(endpoint->buffer[endpoint->read_index] == '\0');

  // catering to both client and upstream
  // rejecting body for client
  // accepting body from upstream
  bool client = endpoint == &conn->client, upstream = endpoint == &conn->upstream;

  if (!client && !upstream)
    return err("verify_endpoint", "Unknown endpoint");

  char *headers_end = NULL;
  if (!(headers_end = strstr(endpoint->buffer, TRAILER)))
  {
    if ((size_t)endpoint->read_index >= BUFFER_SIZE - 1)
    { // no space left
      conn->status = client ? 431 : 500;
      return err("strstr", "Headers too large");
    }
    return true; // read more
  }
  else
    endpoint->headers_found = true;

  headers_end += TRAILER_STR.len; // now past the last \n

  endpoint->headers.data = endpoint->buffer;
  endpoint->headers.len = headers_end - endpoint->buffer;

  // tmp null termination for get_header_value(), so I do not get to the next
  // request or search for the header in the body (if read)
  char org_char = *headers_end;
  *headers_end = '\0';

  Str misc = ERR_STR; // misc str to contain the header value

  if (get_header_value(endpoint->buffer, "Connection", &misc))
  {
    Str *conn_header = &misc;

    if (equals(*conn_header, STR("close"))) // close if either side wants to close
      conn->keep_alive = false;
    else if (equals(*conn_header, STR("keep-alive")))
    {
      if (client)
        conn->keep_alive = true;
      else // only keep alive if client also want to
        conn->keep_alive = conn->keep_alive ? true : false;
    }
    else
    {
      *headers_end = org_char;
      conn->status = client ? 400 : 500;
      return err("verify_connection_header", "Invalid connection header value");
    }
  }

  misc = ERR_STR;
  if (get_header_value(endpoint->buffer, "Content-Length", &misc))
  {
    *headers_end = org_char;
    Str *content_len_str = &misc;

    for (int i = 0; i < content_len_str->len; i++)
      if (!isdigit(content_len_str->data[i]))
      {
        conn->status = client ? 400 : 500;
        return err("isdigit", "Invalid content-length header value");
      }

    // a null terminated str for atoi()
    char *tmp = strndup(content_len_str->data, (size_t)content_len_str->len);
    endpoint->content_len = (size_t)atoi(tmp);
    free(tmp);

    if (!endpoint->content_len) // empty body
      goto read_complete;

    if (endpoint->content_len > 10 * MB)
    {
      conn->status = client ? 413 : 500;
      return err("verify_content_len", "Content too large");
    }

    size_t full_size = (size_t)endpoint->headers.len + endpoint->content_len;

    if (full_size == (size_t)endpoint->read_index) // body read already, but nothing else
      goto read_complete;

    if (full_size < (size_t)endpoint->read_index)
    {                                                  // body read and another request
      endpoint->next_index = (ptrdiff_t)full_size + 1; // will be copied to the start for next read
      goto read_complete;
    }

    endpoint->to_read = full_size - (size_t)endpoint->read_index;

    if (client) // no need to store body for client
      goto disregard_body;

    return true; // store body for upstream
  }
  else if (get_header_value(endpoint->buffer, "Transfer-Encoding", &misc))
  {
    *headers_end = org_char;
    Str *transfer_encoding = &misc;

    if (!equals(*transfer_encoding, STR("chunked")))
    {
      conn->status = client ? 411 : 500;
      return err("verify_encoding", "Encoding method not supported");
    }

    // finding full last chunk or partial from last few bytes
    // worst case, got: '0\r\n\r'
    if (find_last_chunk(endpoint))
      goto read_complete;

    endpoint->chunked = true;

    if (client)
      goto disregard_body;

    return true;
  }
  else
    *headers_end = org_char;

read_complete:
  endpoint->to_read = 0;
  return true;

disregard_body:
  endpoint->read_index = endpoint->headers.len;
  return true;
}

void check_conn(Connection *conn)
{
  if (!conn)
    return;

  assert(conn->state == CHECK_CONN);
  assert(conn->complete);

  if (conn->keep_alive)
    reset_conn(conn); // start to read again from client
  else
    conn->state = CLOSE_CONN;
}

void print_endpoint(const Endpoint *endpoint)
{
  if (!endpoint)
    return;

  puts("\033[1;34mDebug info:\n");
  printf("\033[1;33mBuffer:\033[0;32m %s\n", endpoint->buffer);
  printf("\033[1;33mHeaders:\033[0;32m %.*s\n", (int)endpoint->headers.len, endpoint->headers.data);
  printf("\033[1;33mHeaders len:\033[0;32m %ld\n", endpoint->headers.len);
  printf("\033[1;33mRead index:\033[0;32m %ld\n", endpoint->read_index);
  printf("\033[1;33mWrite index:\033[0;32m %ld\n", endpoint->write_index);
  printf("\033[1;33mTo read:\033[0;32m %zu\n", endpoint->to_read);
  printf("\033[1;33mTo write:\033[0;32m %zu\n", endpoint->to_write);
  printf("\033[1;33mNext index:\033[0;32m %ld\n", endpoint->next_index);
  printf("\033[1;33mContent len:\033[0;32m %zu\n", endpoint->content_len);
  printf("\033[1;33mChunked:\033[0;32m %s\n", endpoint->chunked ? "true" : "false");
  printf("\033[1;33mHeaders found:\033[0;32m %s\n", endpoint->headers_found ? "true" : "false");
  printf("\033[1;33mLast chunk found:\033[0;32m %s\n", endpoint->last_chunk_found);
  puts("\033[1;34mEnd\n\033[0m");
}

bool setup_endpoint_tls(Endpoint *endpoint)
{
  if (!endpoint)
    return err("verify_endpoint", "NULL endpoint pointer passed.");

  if (ssl_context)
  {
    if (!(endpoint->ssl = SSL_new(ssl_context)))
    { // endpoint.ssl will be free during close_conn, no need to handle here in case of error
      ERR_print_errors_fp(stderr);
      return err("SSL_new", NULL);
    }
    else if (!SSL_set_fd(endpoint->ssl, endpoint->fd))
    {
      ERR_print_errors_fp(stderr);
      return err("SSL_set_fd", NULL);
    }
    else if (!SSL_accept(endpoint->ssl))
    {
      ERR_print_errors_fp(stderr);
      return err("SSL_accept", NULL);
    }
  }

  return true;
}
