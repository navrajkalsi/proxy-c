#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "main.h"
#include "poll.h"
#include "request.h"
#include "utils.h"

bool handle_request(EventData *event_data) {
  if (!event_data)
    return set_efault();

  Connection *conn = (Connection *)event_data->data.ptr;

  // event_data SHOULD ALWAYS contain the data as a pointer to a conn
  assert(event_data->data_type == TYPE_PTR_CLIENT);
  assert(conn);

  printf("Read: %zd\n",
         read(conn->client_fd, conn->client_buffer, BUFFER_SIZE));

  return true;
}
