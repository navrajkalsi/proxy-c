#pragma once

#include "connection.h"

// global list, to be implemented in ascending order of ttl
extern Timeout *timeouts_head, *timeouts_tail;

// mallocs new timeout node and enqueues it
// also adds it the timeouts array of the conn
Timeout *init_timeout(Connection *conn, TimeoutType type);

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
