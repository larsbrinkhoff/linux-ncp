#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include "ncp.h"
#include "inet.h"

static pid_t reader_pid = 0;
static pid_t writer_pid = 0;

static int reader (int connection)
{
  unsigned char data[200], *ptr;
  ssize_t n;
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
    for (ptr = data; size > 0; ptr += n, size -= n) {
      n = write (fds[1], data, size);
      if (n <= 0)
        exit (-n);
    }
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

static int copy (int from, int to)
{
  unsigned char data[200], *ptr;
  ssize_t n = read (from, data, sizeof data), m;
  if (n <= 0)
    return n;
  for (ptr = data; n > 0; ptr += m, n -= m) {
    m = write (to, data, n);
    if (m <= 0)
      return m;
  }
  return 1;
}

static void transport (int fd, int connection)
{
  int reader_fd = reader (connection);
  int writer_fd = writer (connection);

  for (;;) {
    fd_set rfds;
    int n;
    FD_ZERO (&rfds);
    FD_SET (fd, &rfds);
    FD_SET (reader_fd, &rfds);
    n = select (33, &rfds, NULL, NULL, NULL);
    if (n <= 0)
      goto end;

    if (FD_ISSET (fd, &rfds)) {
      if (copy (fd, writer_fd) <= 0)
        goto end;
    }
    if (FD_ISSET (reader_fd, &rfds)) {
      if (copy (reader_fd, fd) <= 0)
        goto end;
    }
  }

 end:
  kill (reader_pid, SIGTERM);
  kill (writer_pid, SIGTERM);
}

static void tcp_to_ncp (const char *port, const char *host, const char *sock)
{
  char *foreign_host;
  int connection, foreign_port;
  int fd, s, size;

  s = inet_server (port);
  fd = inet_accept (s, &foreign_host, &foreign_port);
  fprintf (stderr, "Connection from host %s on port %d.\n",
           foreign_host, foreign_port);

  int ncp_host = atoi (host);
  int ncp_sock = atoi (sock);

  size = 8;
  switch (ncp_open (ncp_host, ncp_sock, &size, &connection)) {
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

  transport (fd, connection);

  if (ncp_close (connection) == -1) {
    fprintf (stderr, "NCP close error.\n");
    exit (1);
  }
}

static void ncp_to_tcp (int sock, const char *host, const char *port)
{
  int fd, ncp_host, connection, size;

  size = 8;
  if (ncp_listen (sock, &size, &ncp_host, &connection) == -1) {
    fprintf (stderr, "NCP listen error.\n");
    exit (1);
  }
  fprintf (stderr, "Connection from host %03o on socket %d.\n", ncp_host, sock);

  fd = inet_connect (host, port);
  transport (fd, connection);

  if (ncp_close (connection) == -1) {
    fprintf (stderr, "NCP close error.\n");
    exit (1);
  }
}

static void usage (const char *argv0)
{
  fprintf (stderr, "Usage: %s -T<source TCP port> <host> <socket>\n"
           "or %s -N<source NCP socket> <host> <port>\n", argv0, argv0);
}

int main (int argc, char **argv)
{
  const char *host, *tcp_port = NULL, *number = NULL;
  int opt, ncp_sock = -1;

  while ((opt = getopt (argc, argv, "N:T:")) != -1) {
    switch (opt) {
    case 'N':
      ncp_sock = atoi (optarg);
      if ((ncp_sock & 1) == 0) {
        fprintf (stderr, "Socket must be odd.\n");
        exit (1);
      }
      break;
    case 'T':
      tcp_port = optarg;
      break;
    default:
      usage (argv[0]);
      exit (1);
    }
  }

  host = argv[optind++];
  number = argv[optind++];

  if (argc != optind || (tcp_port != NULL && ncp_sock != -1) ||
      (tcp_port == NULL && ncp_sock == -1)) {
    usage (argv[0]);
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

  if (tcp_port != NULL)
    tcp_to_ncp (tcp_port, host, number);
  else if (ncp_sock != -1)
    ncp_to_tcp (ncp_sock, host, number);

  return 0;
}
