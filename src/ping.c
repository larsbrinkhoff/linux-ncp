#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "ncp.h"

int main (int argc, char **argv)
{
  int host, data = 1, reply;

  if (argc < 2 || argc > 3) {
    fprintf (stderr, "Usage: %s host [data]\n", argv[0]);
    exit (1);
  }

  host = atoi (argv[1]);

  if (argc == 3)
    data = atoi (argv[2]);

  if (ncp_init (NULL) == -1) {
    fprintf (stderr, "NCP initializtion error: %s.\n", strerror (errno));
    if (errno == ECONNREFUSED)
      fprintf (stderr, "Is the NCP server started?\n");
    exit (1);
  }

  printf ("NCP PING host %03o, one octet data: %03o\n", host, data);

  if (ncp_echo (atoi (argv[1]), data, &reply) == -1) {
    fprintf (stderr, "NCP echo error.\n");
    exit (1);
  }

  printf ("One octet from host %03o: %03o\n", host, reply);
}
