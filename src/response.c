#include "response.h"
#include "main.h"
#include "poll.h"
#include "utils.h"
#include <sys/socket.h>
#include <unistd.h>

bool write_error_response(EventData *event_data) {
  if (!event_data)
    return set_efault();

  Connection *conn = event_data->data.ptr;

  conn->client_status =
      ASSIGN_IF_NULL(conn->client_status, "500 Internal Server Error");

  if (!conn || equals(&conn->client_status, &STR("200 OK")))
    conn->client_status = STR("500 Internal Server Error");

  write(conn->client_fd, "HTTP/1.1 ", sizeof "HTTP/1.1");

  write(conn->client_fd, conn->client_status.data, conn->client_status.len);

  write(conn->client_fd, "\r\n\r\n<html><head><title>",
        sizeof "\r\n\r\n<html><head><title");

  write(conn->client_fd, conn->client_status.data, conn->client_status.len);

  write(conn->client_fd, "</title></head><body>",
        sizeof "</title></head><body");

  write(conn->client_fd, conn->client_status.data, conn->client_status.len);

  write(conn->client_fd, "</body></html>\r\n\r\n",
        sizeof "</body></html\r\n\r\n");

  return true;
}
