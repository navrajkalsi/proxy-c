#pragma once

#include <stdbool.h>

typedef struct config
{
  char *port;
  char *canonical_host;
  char *upstream;
  bool accept_all;
} Config;

Config parse_args(int argc, char *argv[]);

// Prints -h help
void print_usage(const char *prg);

void print_args(unsigned int args_parsed, const Config *config);

bool validate_port(char *port);

void free_config(Config *config);
