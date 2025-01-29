#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include "ncp.h"

#define DISCARD_SOCKET   9

static void discard_server (int sock)
{
  int host, connection, size;
  char buffer[1000];

  size = 8;
  if (ncp_listen (sock, &size, &host, &connection) == -1) {
    fprintf (stderr, "NCP listen error.\n");
    exit (1);
  }
  fprintf (stderr, "Connection from host %03o on socket %d.\n", host, sock);

  for (;;) {
    size = sizeof buffer;
    if (ncp_read (connection, buffer, &size) == -1)
      fprintf (stderr, "NCP read error.\n");
    if (size <= 0)
      return;
    fprintf (stderr, "Read %d octets.\n", size);
  }
}

static void usage (const char *argv0)
{
  fprintf (stderr, "Usage: %s [-p socket]\n", argv0);
}

int main (int argc, char **argv)
{
  int opt = 0;
  int sock = -1;

  while ((opt = getopt (argc, argv, "p:")) != -1) {
    switch (opt) {
    case 'p':
      sock = atoi (optarg);
      break;
    default:
      usage (argv[0]);
      exit (1);
    }
  }

  if (argc != optind) {
    usage(argv[0]);
    exit (1);
  }

  if (sock == -1)
    sock = DISCARD_SOCKET;

  if (ncp_init (NULL) == -1) {
    fprintf (stderr, "NCP initialization error: %s.\n", strerror (errno));
    if (errno == ECONNREFUSED)
      fprintf (stderr, "Is the NCP server started?\n");
    else if (errno == EFAULT)
      fprintf (stderr, "Is the NCP environment variable set?\n");
    exit (1);
  }

  discard_server (sock);

  return 0;
}
