#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "main.h"
#include "timeout.h"
#include "utils.h"

// see TimeoutType enum for order
const int TimeoutVals[TIMEOUTTYPES] = {10, 5, 20, 5, 30};

Timeout *timeouts_head = NULL, *timeouts_tail = NULL;

Timeout *init_timeout(Connection *conn, TimeoutType type, time_t ttl)
{
  if (!conn)
  {
    err("null_pointer", strerror(EFAULT));
    return NULL;
  }

  if (type == TIMEOUTTYPES)
  {
    err("verify_timeouttype", "Invalid timeout type");
    return NULL;
  }

  Timeout *timeout = malloc(sizeof(struct timeout));

  if (!timeout)
  {
    err("malloc", strerror(errno));
    return NULL;
  }

  timeout->conn = conn;
  timeout->type = type;
  timeout->created = time(NULL);
  timeout->ttl = TimeoutVals[type];
  timeout->next = NULL;

  if (timeout->created == (time_t)(-1))
  {
    free(timeout);
    err("time", NULL);
    return NULL;
  }

  // adding the timeout to the timeouts array in conn
  if (conn->timeouts[type])
    delete_timeout(conn->timeouts[type]);

  conn->timeouts[type] = timeout;

  enqueue_timeout(timeout);

  return timeout;
}

void free_timeout(Timeout **timeout)
{
  if (!timeout || !*timeout)
    return;

  free(*timeout);
  *timeout = NULL;
}

void enqueue_timeout(Timeout *timeout)
{
  if (!timeout)
    return;

  Timeout *current = timeouts_head;
  time_t now = time(NULL);

  if (!current) // new start
    timeouts_head = timeouts_tail = timeout;
  else if (EXPIRES_IN(timeout) >= EXPIRES_IN(timeouts_tail))
  { // insert at end
    timeouts_tail->next = timeout;
    timeouts_tail = timeout;
  }
  else
    while (true)
    { // look for nearest ttl
      if (current->next && EXPIRES_IN(timeout) > EXPIRES_IN(current->next))
        continue;

      if (!current->next)
        timeouts_tail = timeout;

      timeout->next = current->next;
      current->next = timeout;
      break;
    }
}

Timeout *dequeue_timeout(void)
{
  time_t now = time(NULL);

  if (!timeouts_head || EXPIRES_IN(timeouts_head)) // if no head or head has not expired yet
    return NULL;

  Timeout *timeout = timeouts_head;
  timeouts_head = timeouts_head->next;
  if (!timeouts_head)
    timeouts_tail = NULL;

  return timeout;
}

void clear_expired(void)
{
  Timeout *current = NULL;

  while ((current = dequeue_timeout()))
  {
    puts("cleared");
  }
}
