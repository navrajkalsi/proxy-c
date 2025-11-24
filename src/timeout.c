#include <stdio.h>
#include <time.h>

#include "connection.h"
#include "main.h"
#include "proxy.h"
#include "timeout.h"

// see TimeoutType enum for order
const int TimeoutVals[TIMEOUTTYPES] = {10, 5, 20, 5, 10};

Timeout *timeouts_head = NULL, *timeouts_tail = NULL;

void enqueue_timeout(Timeout *timeout)
{
  if (!timeout)
    return;

  Timeout *current = timeouts_head;
  time_t now = time(NULL);

  timeout->active = true;

  if (!current)
  { // new start
    timeouts_head = timeouts_tail = timeout;
    return;
  }

  if (EXPIRES(timeout) >= EXPIRES(timeouts_tail))
  { // insert at end
    timeouts_tail->next = timeout;
    timeouts_tail = timeout;
    return;
  }

  while (current->next && EXPIRES(timeout) > EXPIRES(current->next))
    ; // look for nearest ttl

  if (!current->next)
    timeouts_tail = timeout;

  timeout->next = current->next;
  current->next = timeout;
}

Timeout *dequeue_timeout(void)
{
  time_t now = time(NULL);

  if (!timeouts_head || EXPIRES(timeouts_head))
    return NULL;

  Timeout *timeout = timeouts_head;
  timeouts_head = timeouts_head->next;
  if (!timeouts_head)
    timeouts_tail = NULL;

  timeout->active = false;

  return timeout;
}

void clear_expired(void)
{
  Timeout *current = NULL;

  while ((current = dequeue_timeout()))
  {
    Connection *conn = current->conn;
    if (conn->state == READ_REQUEST || conn->state == WRITE_REQUEST)
      conn->status = 408;
    else if (conn->state == READ_RESPONSE || conn->state == WRITE_RESPONSE)
      conn->status = 504;
    else // overall conn timeout
      conn->status = 500;

    conn->state = WRITE_ERROR;
    handle_state(conn);
  }
}

void start_conn_timeout(Connection *conn, time_t ttl)
{
  if (!conn)
    return;

  if (conn->conn_timeout.active)
    remove_timeout(&conn->conn_timeout);

  fill_timeout(conn, CONNECTION, ttl);
  enqueue_timeout(&conn->conn_timeout);
}

void start_state_timeout(Connection *conn, TimeoutType type)
{
  if (!conn)
    return;

  if (conn->state_timeout.active)
    remove_timeout(&conn->state_timeout);

  fill_timeout(conn, type, -1);
  enqueue_timeout(&conn->state_timeout);
}

void remove_timeout(Timeout *timeout)
{
  if (!timeout || !timeouts_head)
    return;

  if (timeouts_head == timeout)
  {
    timeouts_head = timeouts_head->next;
    if (!timeouts_head)
      timeouts_tail = NULL;
    timeout->active = false;
    return;
  }

  Timeout *current = timeouts_head;

  for (; current->next != timeout; current = current->next)
    if (!current->next) // not found
      return;

  current->next = timeout->next;

  if (timeouts_tail == timeout)
    timeouts_tail = current;

  timeout->active = false;
}

const char *get_type_string(TimeoutType type)
{
  switch (type)
  {
  case REQUEST_READ:
    return "Request_read";
  case REQUEST_WRITE:
    return "Request_write";
  case RESPONSE_READ:
    return "Response_read";
  case RESPONSE_WRITE:
    return "Response_write";
  case CONNECTION:
    return "Connection";
  default:
    return "";
  }
}

void fill_timeout(Connection *conn, TimeoutType type, time_t ttl)
{
  if (!conn)
    return;

  Timeout *timeout = type == CONNECTION ? &conn->conn_timeout : &conn->state_timeout;

  timeout->conn = conn;
  timeout->type = type;
  timeout->start = time(NULL);
  timeout->ttl = ttl == -1 ? TimeoutVals[type] : ttl;
  timeout->next = NULL;
}

void print_timeouts(void)
{
  int i = 0;
  time_t now = time(NULL);
  puts("Timeouts: [");

  for (Timeout *current = timeouts_head; current; current = current->next)
    printf("\t{%d - Type: %s, Time: %ld, Expires: %ld }\n", ++i, get_type_string(current->type),
           current->ttl, EXPIRES(current));

  printf("] Len: %d\n", i);
}
