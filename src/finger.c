#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "ncp.h"

int main (int argc, char **argv)
{
  char *command;
  char reply[1000];
  int host, connection, size;

  if (argc != 2) {
    fprintf (stderr, "Usage: %s host\n", argv[0]);
    exit (1);
  }

  host = atoi (argv[1]);

  if (ncp_init (NULL) == -1) {
    fprintf (stderr, "NCP initializtion error: %s.\n", strerror (errno));
    if (errno == ECONNREFUSED)
      fprintf (stderr, "Is the NCP server started?\n");
    exit (1);
  }

  printf ("Finger host %03o.\n", host);

  if (ncp_open (host, 0117, &connection) == -1) {
    fprintf (stderr, "NCP open error.\n");
    exit (1);
  }

  command = "Sample Finger command from client.\r\n";
  if (ncp_write (connection, command, strlen (command)) == -1) {
    fprintf (stderr, "NCP write error.\n");
    exit (1);
  }

  size = sizeof reply;
  if (ncp_read (connection, reply, &size) == -1) {
    fprintf (stderr, "NCP read error.\n");
    exit (1);
  }

  write (1, reply, size);

  if (ncp_close (connection) == -1) {
    fprintf (stderr, "NCP close error.\n");
    exit (1);
  }

}
