#include <sys/timerfd.h>

#include "connection.h"

typedef enum timer_types
{
  REQUEST_READ,
  REQUEST_WRITE,
  RESPONSE_READ,
  RESPONSE_WRITE,
  CONNECTION,
  TIMER_TYPES_LEN
} TIMER_TYPES;

static const time_t timer_defaults[TIMER_TYPES_LEN];

// creates a non blockable timer_fd
// all these function will only error incase of programming error
void create_tfd(int *timer_fd);

// arms timer_fd for sec in non repeating way
void arm_tfd(int timer_fd, time_t sec);

// if sec is 0, timeout is determined from defaults array
void arm_conn_tfd(int conn_tfd, time_t sec);

// if sec is 0, timeout is determined from defaults array
void arm_state_tfd(int state_tfd, State state, time_t sec);

// only disarms, does not close the fd
void disarm_tfd(int timer_fd);

// check if the timer expired
bool tfd_expired(int timer_fd);
