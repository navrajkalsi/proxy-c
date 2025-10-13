#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <args.h>
#include <main.h>
#include <utils.h>

Config parse_args(int argc, char *argv[]) {
  Config config = {.port = DEFAULT_PORT,
                   .accept_all = false,
                   .canonical = DEFAULT_CANONICAL};

  int arg;

  unsigned int args_parsed = 0;
  while ((arg = getopt(argc, argv, "ac:hp:v") != -1))
    switch (arg) {
    case 'a':
      config.accept_all = true;
      args_parsed++;
      break;
    case 'c':
      if (!validate_canonical(optarg)) {
        err("validate_canonical()", true);
        exit(EXIT_FAILURE);
      }
      config.canonical = strdup(optarg);
      args_parsed++;
      break;
    case 'h':
      print_usage(argv[0]);
      exit(EXIT_SUCCESS);
    case 'p':
      if (!validate_port(optarg)) {
        enqueue_error("validate_port", strerror(errno));
        err("validate_port()", true);
        exit(EXIT_FAILURE);
      }
      config.port = strdup(optarg);
      args_parsed++;
      break;
    case 'v':
      printf("%s version: %s\n", argv[0], VERSION);
      exit(EXIT_SUCCESS);
    case '?': // If an unknown flag or no argument is passed for an option
              // 'optopt' is set to the flag
      if (optopt == 'c')
        arg_error('c', "requires a valid canonical name");
      else if (optopt == 'p')
        arg_error('p', "requires a valid port number");
      else if (isprint(optopt))
        arg_error((char)optopt, "unknown option");
      else
        fputs("Unknown option character used!\n", stderr);
      exit(EXIT_FAILURE);
    default:
      fputs("Unknown error occurred while parsing arguments\n", stderr);
      exit(EXIT_FAILURE);
    }

  print_args(args_parsed, &config);
  return config;
}

void print_usage(const char *prg) {
  if (!prg) {
    null_ptr("Invalid program pointer");
    return;
  }

  return;
}

void print_args(unsigned int args_parsed, const Config *config) {
  if (!config) {
    null_ptr("Invalid config pointer");
    return;
  }

  printf("%d", args_parsed);

  return;
}

bool validate_port(char *port) {
  if (!port)
    return null_ptr("Invalid port pointer");
  return false;
}

bool validate_canonical(char *canonical) {
  if (!canonical)
    return null_ptr("Invalid canonical pointer");

  return false;
}
