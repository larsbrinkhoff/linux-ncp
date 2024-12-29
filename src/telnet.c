#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include "ncp.h"

#define OLD_TELNET    1
#define NEW_TELNET   23

#define IAC   0377
#define DONT  0376
#define DO    0375
#define WONT  0374
#define WILL  0373
#define SB    0372
#define GA    0371
#define EL    0370
#define EC    0367
#define AYT   0366
#define AO    0365
#define IP    0364
#define BRK   0363
#define MARK  0362
#define NOP   0361
#define SE    0360
#define NUL   0000

#define OPT_BINARY              000
#define OPT_ECHO                001
#define OPT_RECONNECTION        002
#define OPT_SUPPRESS_GO_AHEAD   003
#define OPT_APPROX_MSG_SIZE     004
#define OPT_STATUS              005
#define OPT_TIMING_MARK         006
#define OPT_REMOTE_CTRL_TANDE   007
#define OPT_EXTENDED_ASCII      021
#define OPT_SUPDUP              025
#define OPT_SUPDUP_OUTPUT       026

static char options[] = {
  IAC, DO, OPT_ECHO,
  IAC, DO, OPT_SUPPRESS_GO_AHEAD,
  IAC, WILL, OPT_SUPPRESS_GO_AHEAD,
  0
};

static void option (unsigned char c)
{
  switch (c) {
  case OPT_BINARY:
    break;
  case OPT_ECHO:
    break;
  case OPT_RECONNECTION:
    break;
  case OPT_SUPPRESS_GO_AHEAD:
    break;
  case OPT_APPROX_MSG_SIZE:
    break;
  case OPT_STATUS:
    break;
  case OPT_TIMING_MARK:
    break;
  case OPT_REMOTE_CTRL_TANDE:
    break;
  case OPT_EXTENDED_ASCII:
    break;
  case OPT_SUPDUP:
    break;
  case OPT_SUPDUP_OUTPUT:
    break;
  default:
    break;
  }
}

static int special (unsigned char c, const unsigned char *data)
{
  switch (c) {
  case IAC:
    write (1, &c, 1);
    return 0;
  case DONT:
    option (*data);
    return 1;
  case DO:
    option (*data);
    return 1;
  case WONT:
    option (*data);
    return 1;
  case WILL:
    option (*data);
    return 1;
  case SB:
    return 0;
  case GA:
    return 0;
  case EL:
    return 0;
  case EC:
    write (1, "\b \b", 3);
    return 0;
  case AYT:
    return 0;
  case AO:
    return 0;
  case IP:
    return 0;
  case BRK:
    return 0;
  case MARK:
    return 0;
  case NOP:
    return 0;
  case SE:
    return 0;
  default:
    return 0;
  }
}

static void process (const unsigned char *data, int size)
{
  int i;

  for (i = 0; i < size; i++) {
    switch (data[i]) {
    case NUL:
      break;
    case IAC:
      i++;
      i += special (data[i], &data[i + 1]);
      break;
    default:
      write (1, &data[i], 1);
      break;
    }
  }
}

static void telnet_client (int host, int sock, int new)
{
  unsigned char reply[1000];
  int connection, size;

  printf ("TELNET to host %03o.\n", host);

  switch (ncp_open (host, sock, &connection)) {
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

  if (ncp_write (connection, options, strlen (options)) == -1) {
    fprintf (stderr, "NCP write error.\n");
    exit (1);
  }

  size = sizeof reply;
  if (ncp_read (connection, reply, &size) == -1) {
    fprintf (stderr, "NCP read error.\n");
    exit (1);
  }

  process (reply, size);

  if (ncp_close (connection) == -1) {
    fprintf (stderr, "NCP close error.\n");
    exit (1);
  }
}

static void telnet_server (int sock, int new)
{
  int host, connection;
  char *banner;

  if (ncp_listen (sock, &host, &connection) == -1) {
    fprintf (stderr, "NCP listen error.\n");
    exit (1);
  }

  if (ncp_write (connection, options, strlen (options)) == -1) {
    fprintf (stderr, "NCP write error.\n");
    exit (1);
  }

  banner = "Welcome to Unix.\r\n";
  if (ncp_write (connection, banner, strlen (banner)) == -1) {
    fprintf (stderr, "NCP write error.\n");
    exit (1);
  }

  if (ncp_close (connection) == -1) {
    fprintf (stderr, "NCP close error.\n");
    exit (1);
  }
}

static void usage (const char *argv0)
{
  fprintf (stderr, "Usage: %s -c[no] host\n"
           "or     %s -s[no]\n", argv0, argv0);
}

int main (int argc, char **argv)
{
  int opt, client = 1, server = 0, old = 0, new = 1;
  int host = -1;
  int sock = -1;

  while ((opt = getopt (argc, argv, "cnos")) != -1) {
    switch (opt) {
    case 'c':
      client = 1;
      server = 0;
      break;
    case 'n':
      new = 1;
      old = 0;
      if (sock == -1)
        sock = NEW_TELNET;
      break;
    case 'o':
      new = 0;
      old = 1;
      if (sock == -1)
        sock = OLD_TELNET;
      break;
    case 'p':
      sock = atoi (argv[optind]);
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

  if (argc != optind || (client && server) || (new && old)) {
    usage(argv[0]);
    exit (1);
  }

  if (sock == -1)
    sock = old ? OLD_TELNET : NEW_TELNET;

  if (ncp_init (NULL) == -1) {
    fprintf (stderr, "NCP initializtion error: %s.\n", strerror (errno));
    if (errno == ECONNREFUSED)
      fprintf (stderr, "Is the NCP server started?\n");
    exit (1);
  }

  if (client)
    telnet_client (host, sock, new);
  else if (server)
    telnet_server (sock, new);

  return 0;
}
