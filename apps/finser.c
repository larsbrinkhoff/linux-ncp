#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "ncp.h"

#define SOCKET  0117

int main (int argc, char **argv)
{
  char command[1000];
  char reply[1000];
  int host, connection, size;

  if (argc != 1) {
    fprintf (stderr, "Usage: %s\n", argv[0]);
    exit (1);
  }

  if (ncp_init (NULL) == -1) {
    fprintf (stderr, "NCP initialization error: %s.\n", strerror (errno));
    if (errno == ECONNREFUSED)
      fprintf (stderr, "Is the NCP server started?\n");
    else if (errno == EFAULT)
      fprintf (stderr, "Is the NCP environment variable set?\n");
    exit (1);
  }

  fprintf (stderr, "Listening to socket %d.\n", SOCKET);

  size = 8;
  if (ncp_listen (SOCKET, &size, &host, &connection) == -1) {
    fprintf (stderr, "NCP listen error.\n");
    exit (1);
  }
  fprintf (stderr, "Connection from host %03o.\n", host);

  size = sizeof command;
  if (ncp_read (connection, command, &size) == -1) {
    fprintf (stderr, "NCP read error.\n");
    if (ncp_close (connection) == -1)
      fprintf (stderr, "NCP close error.\n");
    goto unlisten;
  }
  if (size == 0) {
    fprintf (stderr, "Connection closed.\n");
    goto unlisten;
  }
  command[size] = 0;

  snprintf (reply, sizeof reply - 1,
            "Sample response from Finger server.\r\n"
            "Data from client was: \"%s\".\r\n", command);
  size = strlen (reply);
  if (ncp_write (connection, reply, &size) == -1)
    fprintf (stderr, "NCP write error.\n");

  if (size != 0 && ncp_close (connection) == -1)
    fprintf (stderr, "NCP close error.\n");

 unlisten:
  if (ncp_unlisten (SOCKET) == -1) {
    fprintf (stderr, "NCP unlisten error.\n");
    exit (1);
  }

  return 0;
}
