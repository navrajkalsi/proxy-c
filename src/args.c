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
  Config config = {.port = NULL, .accept_all = false, .canonical = NULL};

  int arg;
  unsigned int args_parsed = 0;

  while ((arg = getopt(argc, argv, "ac:hp:v")) != -1)
    switch (arg) {
    case 'a':
      config.accept_all = true;
      args_parsed++;
      break;
    case 'c':
      if (!validate_canonical(optarg)) {
        enqueue_error("validate_canonical",
                      errno ? strerror(errno)
                            : "Invalid canonical name passed");
        if (config.port)
          free(config.port);
        exit(EXIT_FAILURE);
      }
      config.canonical = strdup(optarg);
      args_parsed++;
      break;
    case 'h':
      print_usage(argv[0]);
      if (config.port)
        free(config.port);
      if (config.canonical)
        free(config.canonical);
      exit(EXIT_SUCCESS);
    case 'p':
      if (!validate_port(optarg)) {
        enqueue_error("validate_port", strerror(errno));
        if (config.canonical)
          free(config.canonical);
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
        enqueue_error("parse_args",
                      "Option '-c' requires a valid canonical name");
      else if (optopt == 'p')
        enqueue_error("parse_args", "Option '-p' requires a valid port number");
      else if (isprint(optopt))
        enqueue_error("parse_args", "Unknown option");
      else
        enqueue_error("parse_args", "Unknown option character used!\n");
      exit(EXIT_FAILURE);
    default:
      enqueue_error("parse_args",
                    "Unknown error occurred while parsing arguments");
      exit(EXIT_FAILURE);
    }

  // if not set with flag, verifying default
  if (!config.canonical) {
    if (!(validate_canonical(DEFAULT_CANONICAL))) {
      if (config.port)
        free(config.port);
      enqueue_error("validate_canonical",
                    errno ? strerror(errno) : "Invalid canonical name passed");
      exit(EXIT_FAILURE);
    }
    config.canonical = DEFAULT_CANONICAL;
  }
  if (!config.port) {
    if (!(validate_port(DEFAULT_PORT))) {
      if (config.canonical)
        free(config.canonical);
      enqueue_error("validate_port", NULL);
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
         "-c <canonical> Canonical Name to redirect requests to.\n"
         "-h             Print this help message.\n"
         "-p <port>      Port to listen on.\n"
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

  printf("\nCanonical set to: %s\n"
         "Port set to: %s\n",
         config->canonical, config->port);

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

bool validate_canonical(char *canonical) {
  if (!canonical)
    return set_efault();

  regex_t regex;
  memset(&regex, 0, sizeof regex);
  int status = 0;
  char error_string[256];

  if ((status = regcomp(&regex, URL_REGEX,
                        REG_EXTENDED | REG_NOSUB | REG_ICASE)) != 0) {
    regerror(status, &regex, error_string, sizeof error_string);
    return enqueue_error("regcomp", error_string);
  }

  if ((status = regexec(&regex, canonical, 0, NULL, 0)) != 0) {
    regerror(status, &regex, error_string, sizeof error_string);
    regfree(&regex);
    return enqueue_error("regexec", error_string);
  }

  regfree(&regex);
  return true;
}
