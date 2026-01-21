# Changelog

## [1.0]

### Added

- Initial release.
- One `epoll` instance handles all the file descriptors.
- Support for **one upstream server**.
- Support for different **canonical** & **upstream hosts**.
- Redirection using **301** incase of host mismatch.
- **Regex** for validating hosts.
- **TLS** with `openssl`.
- **Timeouts** for I/O state and full connection.
- **Error reporting** to client.
- **Graceful Shutdown** by handling `SIGINT` & `SIGTERM` signals.
- Configurable options via command-line flags:
  - `-a` → accept connections from **all network interfaces** (default: loopback only).
  - `-c` → canonical host to redirect to.
  - `-h` → print usage.
  - `-p` → port to listen on (default: `1419`).
  - `-s` → use **HTTPS** for client side.
  - `-S` → use **HTTPS** for server side.
  - `-u` → server URL to contact for response.
  - `-v` → print version number.
  - `-w` → print all warnings as errors.

## [2.0]

### Added

- **Protocol Check** for client before parsing the request, done by looking at the first few bytes.
- **Assertions** are used throughout the program to catch bugs and validate any assumptions.
- **SSL Error States** are added to wait for I/O, making for correct error handling.
- **Custom URL Parsing** is done to validate hosts and appropriate headers.
- **Timeouts** using `timerfd` which are handled by new **TIMEDOUT** states for both connection and
  state.
- **Chunked Tracker struct** is added for handling `chunked encoded` bodies.
- **Previous state** is added to `connection struct` to provide context to a few states that need
  require it.

### Changed

- **Null-terminated** strings are completely removed for sensitive data parsing.
- **Passing** of characters to functions is done completely with `STR` macro.
- **Custom URL parsing** is done for validation, instead of using **Regex**.
- **I/O** functions behave correctly for SSL errors and handle them differently from regular
  `read/write` errors.
- Improved `Location header` for **301 Redirects**.
- Timeouts now use `timerfd`s, which go nicely with `epoll`.
- Client and Upstream `ssl_contexts` are now separate vars, resolving a critical bug.
- `-s` & `-S` now behave better.

### Deprecated

- Custom Timeout Implementation.
- Regex Validation for URLs.
