#include "main.h"
#include "args.h"
#include "proxy.h"
#include "upstream.h"
#include "utils.h"

Config config = {.canonical_host = {.host = NULL_STR, .port = NULL_STR},
                 .upstream_host = {.host = NULL_STR, .port = NULL_STR},
                 .listen_port = NULL_STR,
                 .accept_all = false,
                 .log_warnings = false,
                 .client_https = false,
                 .upstream_https = false};
SSL_CTX *ssl_context = NULL;
bool RUNNING = true;
int EPOLL_FD = -1;
int PROXY_FD = -1;
// regex_t origin_regex;

int main(int argc, char *argv[])
{
  print_banner();

  if (!setup_sig_handler())
    err_n_exit("setup_sig_handler", NULL);

  parse_args(argc, argv);

  if (!setup_proxy())
    err_n_exit("setup_proxy", NULL);

  if (!setup_epoll())
    err_n_exit("setup_epoll", NULL);

  if (EPOLL_FD == -1)
    err_n_exit("verify_epoll_fd", "Epoll fd is not valid");

  // loading server info, into global var in proxy.c
  if (!setup_upstream())
    err_n_exit("setup_upstream", NULL);

  if (!start_proxy())
    err_n_exit("start_proxy", NULL);

  // free_upstream_addrinfo();
  // free_active_conns();
  // free_config(&config);
  // if (ssl_context)
  //   SSL_CTX_free(ssl_context);
  // regfree(&origin_regex);
  // EVP_cleanup();
  // return 0;
}
