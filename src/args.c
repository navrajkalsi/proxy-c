#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "main.h"
#include "proxy.h"
#include "url.h"
#include "utils.h"

void parse_args(int argc, char *argv[])
{
  int arg;
  unsigned int args_parsed = 0;
  URL url = {0};      // for parsing hosts
  Str str = NULL_STR; // for parsing hosts

  while ((arg = getopt(argc, argv, "ac:hp:sSu:vw")) != -1)
    switch (arg)
    {
    case 'a':
      config.accept_all = true;
      args_parsed++;
      break;
    case 'c':
      str = (Str){strdup(optarg), strlen(optarg)};

      if (!parse_url(str, &url)) // no need to free, program will free at exit
        err_n_exit("parse_url", NULL);

      if (url.protocol.len || url.params.len || url.frags.len)
        err_n_exit("verify_url", "Canonical Host should only contain host and port(optional)");

      extract_host(&url, &config.canonical_host);
      args_parsed++;
      break;
    case 'h':
      print_usage(argv[0]);
      exit(EXIT_SUCCESS);
    case 'p':
      config.listen_port.data = strdup(optarg);
      config.listen_port.len = strlen(optarg);

      if (!validate_port(config.listen_port, NULL))
        err_n_exit("validate_port", NULL);

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
      str = (Str){strdup(optarg), strlen(optarg)};

      if (!parse_url(str, &url))
        err_n_exit("parse_url", NULL);

      if (url.protocol.len || url.params.len || url.frags.len)
        err_n_exit("verify_url", "Upstream Host should only contain host and port(optional)");

      extract_host(&url, &config.upstream_host);
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
  if (!config.canonical_host.host.len)
  {
    str = (Str){strdup(DEFAULT_CANONICAL_HOST), strlen(DEFAULT_CANONICAL_HOST)};

    if (!parse_url(str, &url))
      err_n_exit("parse_url", NULL);

    if (url.protocol.len || url.params.len || url.frags.len)
      err_n_exit("verify_url",
                 "Default Canonical Host should only contain host and port number(optional)");

    extract_host(&url, &config.canonical_host);
  }

  if (!config.upstream_host.host.len)
  {
    str = (Str){strdup(DEFAULT_UPSTREAM_HOST), strlen(DEFAULT_UPSTREAM_HOST)};

    if (!parse_url(str, &url))
      err_n_exit("parse_url", NULL);

    if (url.protocol.len || url.params.len || url.frags.len)
      err_n_exit("verify_url",
                 "Default Upstream Host should only contain host and port number(optional)");

    extract_host(&url, &config.upstream_host);
  }

  if (!config.listen_port.len)
  {
    config.listen_port.data = strdup(DEFAULT_LISTEN_PORT);
    config.listen_port.len = strlen(DEFAULT_LISTEN_PORT);

    if (!(validate_port(config.listen_port, NULL)))
      err_n_exit("validate_port", "Default Listening Port is invalid");
  }

  print_args(args_parsed);
}

void print_usage(const char *prg)
{
  if (!prg)
    return (void)err("print_usage", "NULL program pointer passed");

  printf("\nUsage: %s [OPTIONS] [ARGS...]\n\n"
         "Options:\n"
         "-a             Accept Incoming Connections from all IPs, defaults to Localhost only.\n"
         "-c             Canonical Host to redirect requests to and match Host header with.\n"
         "-h             Print this help message.\n"
         "-p <port>      Port to listen on.\n"
         "-s             Use HTTPS Protocol for client side.\n"
         "-S             Use HTTPS Protocol for server side.\n"
         "-u <upstream>  Upstream Host to contact for response.\n"
         "-v             Print the version number.\n"
         "-w             Print all warnings with errors.\n",
         prg);
}

void print_args(unsigned int args_parsed)
{
  if (args_parsed)
    printf("\nParsed %u Argument(s).", args_parsed);

  printf("\nCanonical Host set to: " BOLD "%.*s\n" RESET "Upstream Host set to: " BOLD
         "%.*s\n" RESET "Listening Port set to: " BOLD "%.*s\n" RESET
         "Client side protocol set to: " BOLD "%s\n" RESET "Upstream side protocol set to: " BOLD
         "%s\n" RESET "Log Warnings set to: " BOLD "%s\n" RESET,
         (int)config.canonical_host.unparsed.len, config.canonical_host.unparsed.data,
         (int)config.upstream_host.unparsed.len, config.upstream_host.unparsed.data,
         (int)config.listen_port.len, config.listen_port.data,
         config.client_https ? "HTTPS" : "HTTP", config.upstream_https ? "HTTPS" : "HTTP",
         config.log_warnings ? "true" : "false");

  config.accept_all
      ? puts("Proxy Accepting Incoming Connections from " BOLD "all IPs.\n" RESET)
      : puts("Proxy Accepting Incoming Connections from " BOLD "Localhost Only.\n" RESET);
}
