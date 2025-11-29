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

Config parse_args(int argc, char *argv[])
{
  Config config = {.port = NULL,
                   .canonical_host = NULL,
                   .upstream = NULL,
                   .accept_all = false,
                   .log_warnings = false,
                   .client_https = false,
                   .upstream_https = false};

  int arg;
  unsigned int args_parsed = 0;

  while ((arg = getopt(argc, argv, "ac:hp:sSu:vw")) != -1)
    switch (arg)
    {
    case 'a':
      config.accept_all = true;
      args_parsed++;
      break;
    case 'c':
      if (!exec_regex(&origin_regex, optarg))
      {
        err("exec_regex", "Invalid canonical host passed");
        free_config(&config);
        exit(EXIT_FAILURE);
      }
      config.canonical_host = strdup(optarg);
      args_parsed++;
      break;
    case 'h':
      print_usage(argv[0]);
      free_config(&config);
      exit(EXIT_SUCCESS);
    case 'p':
      if (!validate_port(optarg))
      {
        err("validate_port", strerror(errno));
        free_config(&config);
        exit(EXIT_FAILURE);
      }
      config.port = strdup(optarg);
      args_parsed++;
      break;
    case 's':
      config.client_https = true;
      args_parsed++;
      break;
    case 'S':
      config.upstream_https = true;
      args_parsed++;
      break;
    case 'u':
      if (!exec_regex(&origin_regex, optarg))
      {
        err("exec_regex", "Invalid upstream url passed");
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
    case 'w':
      config.log_warnings = true;
      break;
    case '?': // If an unknown flag or no argument is passed for an option
              // 'optopt' is set to the flag
      if (optopt == 'c')
        err("parse_args", "Option '-c' requires a valid canonical host");
      else if (optopt == 'p')
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

  // if not set with flag, verifying default values, using strdup() because
  // config is freed in case of error
  if (!config.canonical_host)
  {
    if (!(exec_regex(&origin_regex, DEFAULT_CANONICAL_HOST)))
    {
      err("exec_regex", "Compiled canonical host is invalid.");
      free_config(&config);
      exit(EXIT_FAILURE);
    }
    config.canonical_host = strdup(DEFAULT_CANONICAL_HOST);
  }

  if (!config.upstream)
  {
    if (!(exec_regex(&origin_regex, DEFAULT_UPSTREAM)))
    {
      err("exec_regex", "Complied upstream URL is invalid.");
      free_config(&config);
      exit(EXIT_FAILURE);
    }
    config.upstream = strdup(DEFAULT_UPSTREAM);
  }

  if (!config.port)
  {
    if (!(validate_port(DEFAULT_PORT)))
    {
      err("validate_port", "Compiled port is invalid");
      free_config(&config);
      exit(EXIT_FAILURE);
    }
    config.port = strdup(DEFAULT_PORT);
  }

  print_args(args_parsed, &config);
  return config;
}

void print_usage(const char *prg)
{
  if (!prg)
    return (void)err("print_usage", "NULL program pointer passed");

  printf("\nUsage: %s [OPTIONS] [ARGS...]\n"
         "Options:\n"
         "-a             Accept Incoming Connections from all IPs, defaults to Localhost only.\n"
         "-c             Canonical Host to redirect requests to."
         "-h             Print this help message.\n"
         "-p <port>      Port to listen on.\n"
         "-s             Use HTTPS Protocol for client side.\n"
         "-S             Use HTTPS Protocol for server side.\n"
         "-u <upstream>  Server URL to contact for response.\n"
         "-v             Print the version number.\n"
         "-w             Print warnings with errors.\n",
         prg);
}

void print_args(unsigned int args_parsed, const Config *config)
{
  if (!config)
    return (void)err("print_args", "NULL config pointer passed");

  if (args_parsed)
    printf("\nParsed %u Argument(s).", args_parsed);

  printf("\nCanonical Host set to: %s\n"
         "Upstream URL set to: %s\n"
         "Listening Port set to: %s\n"
         "Client side protocol set to: %s\n"
         "Upstream side protocol set to: %s\n"
         "Log Warnings set to: %s\n",
         config->canonical_host, config->upstream, config->port,
         config->client_https ? "HTTPS" : "HTTP", config->upstream_https ? "HTTPS" : "HTTP",
         config->log_warnings ? "true" : "false");

  config->accept_all ? puts("Proxy Accepting Incoming Connections from all IPs.\n")
                     : puts("Proxy Accepting Incoming Connections from Localhost Only.\n");
}

bool validate_port(char *port)
{
  if (!port)
    return err("validate_port", "NULL port pointer passed");

  char *end;
  const long port_num = strtol(port, &end, 10);
  if (*end != '\0')
  {
    errno = EINVAL; // not a valid number
    return false;
  }
  if (port_num < 0 || port_num > 65535)
  {
    errno = ERANGE; // out of range
    return false;
  }

  return true;
}

void free_config(Config *config)
{
  if (!config)
    return;

  if (config->canonical_host)
    free(config->canonical_host);

  if (config->upstream)
    free(config->upstream);

  if (config->port)
    free(config->port);
}
