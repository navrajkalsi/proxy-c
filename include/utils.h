#pragma once

#include <stdbool.h>

#include "connection.h"
#include "str.h"

// Always returns false
bool err(const char *function, const char *error);

// Always returns NULL
void *err_null(const char *function, const char *error);

void err_n_exit(const char *function, const char *error);

// non critical logging
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

// returns 0 on error
int get_active_conn_index(const Connection *conn);

void log_state(const char *point_of_call, const Connection *conn);

void log_ssl_error(int ssl_get_error_ret);

bool str_to_long(Str str, long *num);

bool str_to_long_hex(Str str, long *num);

bool set_date_str(Str date);
