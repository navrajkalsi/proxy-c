#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "main.h"
#include "utils.h"

Config parse_args(int argc, char *argv[]) {
  Config config = {.port = NULL, .accept_all = false, .upstream = NULL};

  int arg;
  unsigned int args_parsed = 0;

  while ((arg = getopt(argc, argv, "ahp:u:v")) != -1)
    switch (arg) {
    case 'a':
      config.accept_all = true;
      args_parsed++;
      break;
    case 'h':
      print_usage(argv[0]);
      if (config.port)
        free(config.port);
      if (config.upstream)
        free(config.upstream);
      exit(EXIT_SUCCESS);
    case 'p':
      if (!validate_port(optarg)) {
        err("validate_port", strerror(errno));
        if (config.upstream)
          free(config.upstream);
        exit(EXIT_FAILURE);
      }
      config.port = strdup(optarg);
      args_parsed++;
      break;
    case 'u':
      if (!validate_upstream(optarg)) {
        err("validate_url",
            errno ? strerror(errno) : "Invalid upstream url passed");
        if (config.port)
          free(config.port);
        exit(EXIT_FAILURE);
      }
      config.upstream = strdup(optarg);
      args_parsed++;
      break;
    case 'v':
      printf("%s version: %s\n", argv[0], VERSION);
      exit(EXIT_SUCCESS);
    case '?': // If an unknown flag or no argument is passed for an option
              // 'optopt' is set to the flag
      if (optopt == 'p')
        err("parse_args", "Option '-p' requires a valid port number");
      else if (optopt == 'u')
        err("parse_args", "Option '-u' requires a valid upstream url");
      else if (isprint(optopt))
        err("parse_args", "Unknown option");
      else
        err("parse_args", "Unknown option character used!\n");
      exit(EXIT_FAILURE);
    default:
      err("parse_args", "Unknown error occurred while parsing arguments");
      exit(EXIT_FAILURE);
    }

  // if not set with flag, verifying default
  if (!config.upstream) {
    if (!(validate_upstream(DEFAULT_UPSTREAM))) {
      if (config.port)
        free(config.port);
      err("validate_url",
          errno ? strerror(errno) : "Invalid upstream url passed");
      exit(EXIT_FAILURE);
    }
    config.upstream = DEFAULT_UPSTREAM;
  }
  if (!config.port) {
    if (!(validate_port(DEFAULT_PORT))) {
      if (config.upstream)
        free(config.upstream);
      err("validate_port", NULL);
      exit(EXIT_FAILURE);
    }
    config.port = DEFAULT_PORT;
  }

  print_args(args_parsed, &config);
  return config;
}

void print_usage(const char *prg) {
  if (!prg)
    return;

  printf("\nUsage: %s [OPTIONS] [ARGS...]\n"
         "Options:\n"
         "-a             Accept Incoming Connections from all IPs, defaults "
         "to Localhost only.\n"
         "-h             Print this help message.\n"
         "-p <port>      Port to listen on.\n"
         "-u <upstream>  Server URL to redirect requests to.\n"
         "-v             Print the version number.\n",
         prg);
}

void print_args(unsigned int args_parsed, const Config *config) {
  if (!config) {
    null_ptr("Invalid config pointer");
    return;
  }

  if (args_parsed)
    printf("\nParsed %u Argument(s).", args_parsed);

  printf("\nUpstream URL set to: %s\n"
         "Listening Port set to: %s\n",
         config->upstream, config->port);

  config->accept_all
      ? puts("Proxy Accepting Incoming Connections from all IPs.\n")
      : puts("Proxy Accepting Incoming Connections from Localhost Only.\n");
}

bool validate_port(char *port) {
  if (!port)
    return set_efault();

  char *end;
  const long port_num = strtol(port, &end, 10);
  if (*end != '\0') {
    errno = EINVAL; // not a valid number
    return false;
  }
  if (port_num < 0 || port_num > 65535) {
    errno = ERANGE; // out of range
    return false;
  }

  return true;
}

bool validate_upstream(char *upstream) {
  if (!upstream)
    return set_efault();

  return exec_regex(&origin_regex, upstream);
}
