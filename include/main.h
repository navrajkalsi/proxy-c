#pragma once

#define VERSION "0.1"

// args.h specific
#define DEFAULT_PORT "1419"
#define DEFAULT_CANONICAL "https://domain.com"
#ifndef URL_REGEX
#define URL_REGEX "^https?:\\/\\/(www\\.)?[[:alnum:]-]+\\.[[:alpha:]]{2,}\\/?$"
#endif

// utils.h specific
#ifndef TERMINAL_WIDTH
#define TERMINAL_WIDTH 80
#endif
