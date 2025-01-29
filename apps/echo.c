#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include "ncp.h"

#define ECHO_SOCKET   7

static void echo_client (int host, int sock)
{
  int connection, n, byte_size;
  char buffer[1000], *ptr;
  ssize_t size;

  size = 8;
  switch (ncp_open (host, sock, &byte_size, &connection)) {
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

  for (;;) {
    size = read (0, buffer, sizeof buffer);
    if (size == -1)
      fprintf (stderr, "Read error.\n");
    if (size <= 0)
      goto end;
    for (ptr = buffer; size > 0; ptr += n, size -= n) {
      n = size;
      if (ncp_write (connection, ptr, &n) == -1)
        fprintf (stderr, "NCP read error.\n");
      if (n <= 0)
        goto end;
    }

    n = sizeof buffer;
    if (ncp_read (connection, buffer, &n) == -1)
      fprintf (stderr, "NCP read error.\n");
    if (n <= 0)
      goto end;
    for (ptr = buffer; n > 0; n -= size) {
      size = write (1, ptr, n);
      if (size < 0)
        fprintf (stderr, "Write error.\n");
      if (size <= 0)
        goto end;
    }
  }

 end:
  if (ncp_close (connection) == -1) {
    fprintf (stderr, "NCP close error.\n");
    exit (1);
  }
}

static void echo_server (int sock)
{
  int host, connection, size, n;
  char buffer[1000], *ptr;

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
    for (ptr = buffer; size > 0; ptr += n, size -= n) {
      n = size;
      if (ncp_write (connection, ptr, &n) == -1)
        fprintf (stderr, "NCP read error.\n");
      if (n <= 0)
        return;
    }
  }
}

static void usage (const char *argv0)
{
  fprintf (stderr, "Usage: %s -c [-p socket] host\n"
           "or %s -s [-p socket]\n", argv0, argv0);
}

int main (int argc, char **argv)
{
  int opt, client = 1, server = 0;
  int host = -1;
  int sock = -1;

  while ((opt = getopt (argc, argv, "csp:")) != -1) {
    switch (opt) {
    case 'c':
      client = 1;
      server = 0;
      break;
    case 'p':
      sock = atoi (optarg);
      break;
    case 's':
      client = 0;
      server = 1;
      break;
    default:
      usage (argv[0]);
      exit (1);
    }
  }

  if (client)
    host = atoi (argv[optind++]);

  if (argc != optind || (client && server)) {
    usage(argv[0]);
    exit (1);
  }

  if (sock == -1)
    sock = ECHO_SOCKET;

  if (ncp_init (NULL) == -1) {
    fprintf (stderr, "NCP initialization error: %s.\n", strerror (errno));
    if (errno == ECONNREFUSED)
      fprintf (stderr, "Is the NCP server started?\n");
    else if (errno == EFAULT)
      fprintf (stderr, "Is the NCP environment variable set?\n");
    exit (1);
  }

  if (client)
    echo_client (host, sock);
  else if (server)
    echo_server (sock);

  return 0;
}
