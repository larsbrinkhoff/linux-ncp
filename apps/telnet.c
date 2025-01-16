#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include "ncp.h"
#include "tty.h"

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

#define OMARK         0200
#define OBREAK        0201
#define ONOP          0202
#define ONOECHO       0203
#define OECHO         0204
#define OHIDE         0205
#define OASCII        0240
#define OTRANSPARENT  0241
#define OEBCDIC       0242

static pid_t reader_pid = 0;
static pid_t writer_pid = 0;

static char client_options[] = {
  IAC, DO, OPT_ECHO,
  IAC, DO, OPT_SUPPRESS_GO_AHEAD,
  IAC, WILL, OPT_SUPPRESS_GO_AHEAD,
};

static char server_options[] = {
  IAC, DONT, OPT_ECHO,
  IAC, DO, OPT_SUPPRESS_GO_AHEAD,
  IAC, WILL, OPT_SUPPRESS_GO_AHEAD,
  IAC, WILL, OPT_ECHO,
};

static void option (int fd)
{
  unsigned char c;
  read (fd, &c, 1);
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

static void special (unsigned char c, int rfd, int wfd)
{
  switch (c) {
  case IAC:
    write (wfd, &c, 1);
    return;
  case DONT:
    option (rfd);
    return;
  case DO:
    option (rfd);
    return;
  case WONT:
    option (rfd);
    return;
  case WILL:
    option (rfd);
    return;
  case SB:
    return;
  case GA:
    return;
  case EL:
    return;
  case EC:
    write (wfd, "\b \b", 3);
    return;
  case AYT:
    return;
  case AO:
    return;
  case IP:
    return;
  case BRK:
    return;
  case MARK:
    return;
  case NOP:
    return;
  case SE:
    return;
  default:
    return;
  }
}

static void process_new (unsigned char data, int rfd, int wfd)
{
  switch (data) {
  case NUL:
    break;
  case IAC:
    read (rfd, &data, 1);
    special (data, rfd, wfd);
    break;
  default:
    write (wfd, &data, 1);
    break;
  }
}

static void process_old (unsigned char data, int rfd, int wfd)
{
  unsigned char crlf[] = { 015, 012 };
  switch (data) {
  case NUL:
    break;
  case 001: case 002: case 003: case 004: case 005: case 006:
    break;
  case 016: case 017: case 020: case 021: case 022: case 023:
  case 024: case 025: case 026: case 027: case 030: case 031:
  case 032:           case 034: case 035: case 036: case 037:
    break;
  case 0177:
    break;
  case 015:
    read (rfd, &data, 1);
    if (data == NUL)
      write (1, crlf, 1);
    else if (data == 012)
      write (1, crlf, 2);
    else
      fprintf (stderr, "[CR without LF or NUL]");
  case OMARK:
    break;
  case OBREAK:
    fprintf (stderr, "[BREAK]"); fflush (stderr);
    break;
  case ONOP:
    fprintf (stderr, "[NOP]"); fflush (stderr);
    break;
  case ONOECHO:
    fprintf (stderr, "[NOECHO]"); fflush (stderr);
    break;
  case OECHO:
    fprintf (stderr, "[ECHO]"); fflush (stderr);
    break;
  case OHIDE:
    fprintf (stderr, "[HIDE]"); fflush (stderr);
    break;
  case OASCII:
    fprintf (stderr, "[ASCII]"); fflush (stderr);
    break;
  case OTRANSPARENT:
    fprintf (stderr, "[TRANSPARENT]"); fflush (stderr);
    break;
  case OEBCDIC:
    fprintf (stderr, "[EBCDIC]"); fflush (stderr);
    break;
  default:
    write (wfd, &data, 1);
  }
}

static int reader (int connection)
{
  unsigned char data[200];
  int fds[2];
  int size;

  if (pipe (fds) == -1)
    exit (1);

  reader_pid = fork();
  if (reader_pid) {
    close (fds[1]);
    return fds[0];
  }
  close (fds[0]);

  if (ncp_init (NULL) == -1)
    exit (1);

  for (;;) {
    size = sizeof data;
    if (ncp_read (connection, data, &size) == -1) {
      fprintf (stderr, "NCP read error.\n");
      exit (1);
    }
    if (size == 0)
      exit (0);
    if (write (fds[1], data, size) == -1)
      exit (1);
  }
}

static int writer (int connection)
{
  unsigned char data[200], *ptr;
  ssize_t n, m;
  int fds[2];
  int size;

  if (pipe (fds) == -1)
    exit (1);

  writer_pid = fork();
  if (writer_pid) {
    close (fds[0]);
    return fds[1];
  }
  close (fds[1]);

  int flags = fcntl (fds[0], F_GETFL);
  fcntl (fds[0], F_SETFL, flags | O_NONBLOCK);

  if (ncp_init (NULL) == -1)
    exit (1);

  for (;;) {
    n = read (fds[0], data, 200);
    if (n == 0)
      exit (0);
    if (n < 0)
      continue;
    ptr = data;
    for (ptr = data, m = n; m > 0; ptr += size, m -= size) {
      size = m;
      if (ncp_write (connection, ptr, &size) == -1) {
        fprintf (stderr, "NCP write error.\n");
        exit (1);
      }
      if (size == 0)
        exit (0);
    }
  }
}

static void telnet_client (int host, int sock,
                           void (*process) (unsigned char, int, int))
{
  int connection;
  int reader_fd, writer_fd;
  size_t size;

  printf ("TELNET to host %03o.\n", host);

  tty_raw ();

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

  reader_fd = reader (connection);
  writer_fd = writer (connection);

  size = sizeof client_options;
  if (write (writer_fd, client_options, size) == -1) {
    fprintf (stderr, "write error.\n");
    exit (1);
  }

  for (;;) {
    fd_set rfds;
    int n;
    FD_ZERO (&rfds);
    FD_SET (0, &rfds);
    FD_SET (reader_fd, &rfds);
    n = select (reader_fd + 1, &rfds, NULL, NULL, NULL);
    if (n <= 0)
      break;

    if (FD_ISSET (0, &rfds)) {
      unsigned char data;
      read (0, &data, 1);
      if (data == 035)
        goto end;
      write (writer_fd, &data, 1);
    }
    if (FD_ISSET (reader_fd, &rfds)) {
      unsigned char data;
      n = read (reader_fd, &data, 1);
      if (n == 1)
        process (data, reader_fd, 1);
    }
  }

 end:
  kill (reader_pid, SIGTERM);
  kill (writer_pid, SIGTERM);
  tty_restore ();
  printf ("TELNET> quit\n");

  if (ncp_close (connection) == -1) {
    fprintf (stderr, "NCP close error.\n");
    exit (1);
  }
}

static void telnet_server (int sock, int new,
                           void (*process) (unsigned char, int, int))
{
  int host, connection, size;
  int reader_fd, writer_fd;
  char *banner;

  if (ncp_listen (sock, &host, &connection) == -1) {
    fprintf (stderr, "NCP listen error.\n");
    exit (1);
  }

  reader_fd = reader (connection);
  writer_fd = writer (connection);

  size = sizeof server_options;
  if (write (writer_fd, server_options, size) == -1) {
    fprintf (stderr, "write error.\n");
    exit (1);
  }

  banner = "Welcome to Unix.\r\n";
  size = strlen (banner);
  if (write (writer_fd, banner, size) == -1) {
    fprintf (stderr, "write error.\n");
    exit (1);
  }

  char *cmd[] = { "sh", NULL };
  int fd = tty_run (cmd);

  int flags = fcntl (fd, F_GETFL);
  fcntl (fd, F_SETFL, flags | O_NONBLOCK);
  flags = fcntl (reader_fd, F_GETFL);
  fcntl (reader_fd, F_SETFL, flags | O_NONBLOCK);

  for (;;) {
    fd_set rfds;
    int n = fd > reader_fd ? fd : reader_fd;
    FD_ZERO (&rfds);
    FD_SET (fd, &rfds);
    FD_SET (reader_fd, &rfds);
    n = select (n + 1, &rfds, NULL, NULL, NULL);
    if (n <= 0)
      break;

    if (FD_ISSET (fd, &rfds)) {
      unsigned char data[200];
      ssize_t n = read (fd, data, sizeof data);
      if (n <= 0)
        goto end;
      data[n] = 0;
      write (writer_fd, data, n);
    }
    if (FD_ISSET (reader_fd, &rfds)) {
      unsigned char data;
      ssize_t n = read (reader_fd, &data, 1);
      if (n <= 0)
        goto end;
      process (data, reader_fd, fd);
    }
  }

 end:
  kill (reader_pid, SIGTERM);
  kill (writer_pid, SIGTERM);

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

  while ((opt = getopt (argc, argv, "cnosp:")) != -1) {
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

  if (argc != optind || (client && server) || (new && old)) {
    usage(argv[0]);
    exit (1);
  }

  if (sock == -1)
    sock = old ? OLD_TELNET : NEW_TELNET;

  if (ncp_init (NULL) == -1) {
    fprintf (stderr, "NCP initialization error: %s.\n", strerror (errno));
    if (errno == ECONNREFUSED)
      fprintf (stderr, "Is the NCP server started?\n");
    exit (1);
  }

  if (client)
    telnet_client (host, sock, new ? process_new : process_old);
  else if (server)
    telnet_server (sock, new, new ? process_new : process_old);

  return 0;
}
