#pragma once

#include <stdbool.h> //

// Always returns false
bool err(const char *function, const char *error);

void err_n_exit(const char *function, const char *error);

// non fatal logging
bool warn(const char *function, const char *warning);

bool setup_sig_handler(void);

void handle_shutdown(int sig);

void handle_sigpipe(int sig);

// prints number of active connections
void print_active_num(void);

void print_banner(void);

// out should point to a memory that can hold the final string
// check before if num is 0, the function does not handle 0 as num
// this function does not malloc!
void int_to_string(int num, char *out);

const char *get_state_string(int state);

void log_state(int state);
