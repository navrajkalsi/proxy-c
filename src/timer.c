#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sys/timerfd.h>

#include "timer.h"
#include "utils.h"

// indices corresponding to timer_types enum
static const time_t timer_defaults[TIMER_TYPES_LEN] = {15, 10, 30, 10, 45};

void create_tfd(int *timer_fd)
{
  assert(timer_fd);

  if ((*timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)) == -1)
    err_n_exit("timerfd_create", strerror(errno));
}

void arm_tfd(int timer_fd, time_t sec)
{
  struct itimerspec spec = {.it_interval = {0, 0}, .it_value = {.tv_sec = sec, .tv_nsec = 0}};

  if (timerfd_settime(timer_fd, 0, &spec, NULL) == -1)
    err_n_exit("timerfd_settime", strerror(errno));
}

void arm_conn_tfd(int conn_tfd, time_t sec)
{
  return arm_tfd(conn_tfd, sec ? sec : timer_defaults[CONNECTION]);
}

void arm_state_tfd(int state_tfd, State state, time_t sec)
{
  if (sec)
    return arm_tfd(state_tfd, sec);

  switch (state)
  {
  case READ_REQUEST:
    return arm_tfd(state_tfd, timer_defaults[REQUEST_READ]);
  case WRITE_REQUEST:
    return arm_tfd(state_tfd, timer_defaults[REQUEST_WRITE]);
  case READ_RESPONSE:
    return arm_tfd(state_tfd, timer_defaults[RESPONSE_READ]);
  case WRITE_RESPONSE:
    return arm_tfd(state_tfd, timer_defaults[RESPONSE_WRITE]);
  default:
    assert(false); // logic error
  }
}

bool tfd_expired(int timer_fd)
{
  struct itimerspec spec;

  if (timerfd_gettime(timer_fd, &spec) == -1)
    err_n_exit("timerfd_gettime", strerror(errno));

  return spec.it_value.tv_sec == 0 && spec.it_value.tv_nsec == 0;
}
