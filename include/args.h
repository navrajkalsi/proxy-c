#pragma once

#include <stdbool.h>

// fills global config var
void parse_args(int argc, char *argv[]);

// Prints -h help
void print_usage(const char *prg);

void print_args(unsigned int args_parsed);
