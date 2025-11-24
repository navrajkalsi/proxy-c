#pragma once

#include <stdbool.h>
#include <time.h>

typedef struct connection Connection;

typedef enum
{
  REQUEST_READ,
  REQUEST_WRITE,
  RESPONSE_READ,
  RESPONSE_WRITE,
  CONNECTION,
  TIMEOUTTYPES // len of enum
} TimeoutType;

typedef struct timeout
{
  Connection *conn; // what conn to close in case timeout expires
  time_t start;     // time when timeout starts
  time_t ttl;       // when does the timeout expire
  bool active;      // is the timeout currently in the list
  struct timeout *next;
} Timeout;

// will contain int timeouts at correspoding state indices
extern const int TimeoutVals[TIMEOUTTYPES];

// global list, to be implemented in ascending order of ttl
extern Timeout *timeouts_head, *timeouts_tail;

// adds timeout to global list and marks it active, preserving expires order
void enqueue_timeout(Timeout *timeout);

// removes the first timeout which has expired, and marks it inactive
// intended to be used in a loop to dequeue all expired timeouts
Timeout *dequeue_timeout(void);

// dequeues all dead conns
void clear_expired(void);

// enqueues the timeout after setting its ttl
// pass ttl as -1 to use default value
// use for keep-alive timeout as well
void start_conn_timeout(Connection *conn, time_t ttl);

// uses default timeouts depending on the state
void start_state_timeout(Connection *conn, TimeoutType type);

// removes timeout entry from the list, and marks it as inactive
void remove_timeout(Timeout *timeout);

// automatically determines created and ttl (if -1) based on type
void fill_timeout(Connection *conn, TimeoutType type, time_t ttl);
