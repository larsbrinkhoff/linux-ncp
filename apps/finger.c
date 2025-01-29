#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "ncp.h"

int main (int argc, char **argv)
{
  char command[1000];
  char reply[1000];
  int host, connection, size;

  if (argc < 2 || argc > 3) {
    fprintf (stderr, "Usage: %s host [user(s)]\n", argv[0]);
    exit (1);
  }

  host = atoi (argv[1]);

  if (ncp_init (NULL) == -1) {
    fprintf (stderr, "NCP initialization error: %s.\n", strerror (errno));
    if (errno == ECONNREFUSED)
      fprintf (stderr, "Is the NCP server started?\n");
    else if (errno == EFAULT)
      fprintf (stderr, "Is the NCP environment variable set?\n");
    exit (1);
  }

  printf ("Finger host %03o.\n", host);

  size = 8;
  switch (ncp_open (host, 0117, &size, &connection)) {
  case 0:
    break;
  case -1:
  default:
    fprintf (stderr, "NCP open error.\n");
    exit (1);
  case -2:
    fprintf (stderr, "Open refused.\n");
    exit (1);
  }

  size = snprintf (command, sizeof command, "%s\r\n",
                   argc == 3 ? argv[2] : "");
  if (ncp_write (connection, command, &size) == -1) {
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
