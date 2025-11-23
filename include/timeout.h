#pragma once

#include <time.h>

typedef struct connection Connection;

typedef enum
{
  CLIENT_READ,
  UPSTREAM_WRITE,
  UPSTREAM_READ,
  CLIENT_WRITE,
  CONNECTION,
  TIMEOUTTYPES
} TimeoutType;

// will contain int timeouts at correspoding state indices
extern const int TimeoutVals[TIMEOUTTYPES];

typedef struct timeout
{
  Connection *conn; // what conn to close in case timeout expires
  TimeoutType type; // each conn gets one timeout struct for each type
  time_t created;   // time when timeout starts
  time_t ttl;       // when does the timeout expire
  struct timeout *next;
} Timeout;

// global list, to be implemented in ascending order of ttl
extern Timeout *timeouts_head, *timeouts_tail;

// mallocs new timeout node and enqueues it
// also adds it the timeouts array of the conn
Timeout *init_timeout(Connection *conn, TimeoutType type, time_t ttl);

void free_timeout(Timeout **timeout);

// adds timeout to global list, preserving expires_in order
void enqueue_timeout(Timeout *timeout);

// removes the first timeout which has expired
// intended to be used in a loop to dequeue all expired timeouts
Timeout *dequeue_timeout(void);

// dequeues all dead conns and closes them
void clear_expired(void);

// removes timeout entry and frees it
void delete_timeout(Timeout *timeout);
