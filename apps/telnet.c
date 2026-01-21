#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include "ncp.h"
#include <sys/wait.h>
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

static const unsigned char old_client_options[] = {
  OECHO, NUL
};

static const unsigned char old_server_options[] = {
  ONOECHO, NUL
};

static const unsigned char new_client_options[] = {
  IAC, DO, OPT_ECHO,
  IAC, DO, OPT_SUPPRESS_GO_AHEAD,
  IAC, WILL, OPT_SUPPRESS_GO_AHEAD,
  NUL
};

static const unsigned char new_server_options[] = {
  IAC, DONT, OPT_ECHO,
  IAC, DO, OPT_SUPPRESS_GO_AHEAD,
  IAC, WILL, OPT_SUPPRESS_GO_AHEAD,
  IAC, WILL, OPT_ECHO,
  NUL
};

static const unsigned char bin_options[] = {
  NUL
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
      write (wfd, crlf, 1);
    else if (data == 012)
      write (wfd, crlf, 2);
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

static void process_bin (unsigned char data, int rfd, int wfd)
{
  write (wfd, &data, 1);
}


static int reader (int connection)
{
  unsigned char data[200];
  int fds[2];
  int size;
  ssize_t n = 0;
  if (pipe (fds) == -1)
    exit (1);
  fcntl (fds[0], F_SETFD, FD_CLOEXEC);
  fcntl (fds[1], F_SETFD, FD_CLOEXEC);
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
    n = write (fds[1], data, size);
    if (n <= 0) {
      if (n < 0 && errno == EPIPE) {
        fprintf (stderr, "DEBUG: reader child exiting cleanly on EPIPE.\n");
        exit (0);
      }
      fprintf (stderr, "DEBUG: reader child exiting on write error or EOF.\n");
      exit (1);
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
  fcntl (fds[0], F_SETFD, FD_CLOEXEC);
  fcntl (fds[1], F_SETFD, FD_CLOEXEC);

  writer_pid = fork();
  if (writer_pid) {
    close (fds[0]);
    return fds[1];
  }
  close (fds[1]);

  if (ncp_init (NULL) == -1)
    exit (1);

  for (;;) {
    n = read (fds[0], data, 200);
    if (n == 0)
      exit (0);
    if (n < 0)
      exit (1);
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
                           void (*process) (unsigned char, int, int),
                           const unsigned char *options)
{
  int connection, byte_size;
  int reader_fd, writer_fd;
  size_t size;

  printf ("TELNET to host %03o.\n", host);

  byte_size = 8;
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

  reader_fd = reader (connection);
  writer_fd = writer (connection);

  size = strlen ((const char *)options);
  if (write (writer_fd, options, size) == -1) {
    fprintf (stderr, "write error.\n");
    exit (1);
  }

  tty_raw ();

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
      if (read (0, &data, 1) <= 0)
        goto end;
      if (data == 035)
        goto quit;
      if (write (writer_fd, &data, 1) <= 0)
        goto end;
    }
    if (FD_ISSET (reader_fd, &rfds)) {
      unsigned char data;
      n = read (reader_fd, &data, 1);
      if (n == 1)
        process (data, reader_fd, 1);
      if (n <= 0)
        goto end;
    }
  }

 quit:
  printf ("TELNET> quit\r\n");
 end:
  fprintf (stderr, "DEBUG: client shutting down.\n");
  tty_restore ();

  fprintf (stderr, "DEBUG: signaling reader child (PID %d) to unblock from ncp_read.\n", reader_pid);
  kill (reader_pid, SIGTERM);

  fprintf (stderr, "DEBUG: closing pipes to children.\n");
  close (reader_fd);
  close (writer_fd);

  fprintf (stderr, "DEBUG: waiting for reader child (PID %d).\n", reader_pid);
  waitpid (reader_pid, NULL, 0);
  fprintf (stderr, "DEBUG: reader child reaped.\n");

  fprintf (stderr, "DEBUG: waiting for writer child (PID %d).\n", writer_pid);
  waitpid (writer_pid, NULL, 0);
  fprintf (stderr, "DEBUG: writer child reaped.\n");

  if (ncp_close (connection) == -1) {
    fprintf (stderr, "NCP close error.\n");
    exit (1);
  }
  fprintf (stderr, "DEBUG: client shutdown complete.\n");
}

static void telnet_server (int host, int sock,
                           void (*process) (unsigned char, int, int),
                           const unsigned char *options)
{
  int connection, size;
  int reader_fd, writer_fd;
  char *banner;
  pid_t shell_pid;
  int fd;

  fprintf (stderr, "Listening to socket %d.\n", sock);

  size = 8;
  if (ncp_listen (sock, &size, &host, &connection) == -1) {
    fprintf (stderr, "NCP listen error.\n");
    exit (1);
  }
  fprintf (stderr, "Connection from host %03o.\n", host);

  reader_fd = reader (connection);
  writer_fd = writer (connection);

  size = strlen ((const char *)options);
  if (write (writer_fd, options, size) == -1) {
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
  shell_pid = tty_run (cmd, &fd);

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
      goto end;

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
  fprintf (stderr, "DEBUG: server shutting down.\n");

  fprintf (stderr, "DEBUG: closing pipes to children.\n");
  close (reader_fd);
  close (writer_fd);

  fprintf (stderr, "DEBUG: waiting for reader child (PID %d).\n", reader_pid);
  waitpid (reader_pid, NULL, 0);
  fprintf (stderr, "DEBUG: reader child reaped.\n");

  fprintf (stderr, "DEBUG: waiting for writer child (PID %d).\n", writer_pid);
  waitpid (writer_pid, NULL, 0);
  fprintf (stderr, "DEBUG: writer child reaped.\n");

  fprintf (stderr, "DEBUG: terminating shell process group (PID %d).\n", shell_pid);
  killpg (shell_pid, SIGHUP);
  waitpid (shell_pid, NULL, 0);
  fprintf (stderr, "DEBUG: shell process reaped.\n");

  if (ncp_close (connection) == -1)
    fprintf (stderr, "NCP close error.\n");

  if (ncp_unlisten (sock) == -1) {
    fprintf (stderr, "NCP unlisten error.\n");
    exit (1);
  }
  fprintf (stderr, "DEBUG: server shutdown complete.\n");
}

static void usage (const char *argv0, int code)
{
  fprintf (stderr, "Usage: %s -c[bno] host\n"
           "or %s -s[bno]\n", argv0, argv0);
  if (code >= 0)
    exit (code);
}

int main (int argc, char **argv)
{
  void (*telnet) (int, int,
                  void (*) (unsigned char, int, int),
                  const unsigned char *) = NULL;
  void (*process) (unsigned char, int, int) = NULL;
  const unsigned char *client_options = NULL;
  const unsigned char *server_options = NULL;
  const unsigned char *options;
  int opt;
  int host = -1;
  int sock = -1;

  while ((opt = getopt (argc, argv, "bcnosp:")) != -1) {
    switch (opt) {
    case 'b':
      if (process != NULL)
        usage (argv[0], 1);
      process = process_bin;
      client_options = bin_options;
      server_options = bin_options;
      break;
    case 'c':
      if (telnet != NULL)
        usage (argv[0], 1);
      telnet = telnet_client;
      break;
    case 'n':
      if (process != NULL)
        usage (argv[0], 1);
      process = process_new;
      client_options = new_client_options;
      server_options = new_server_options;
      if (sock == -1)
        sock = NEW_TELNET;
      break;
    case 'o':
      if (process != NULL)
        usage (argv[0], 1);
      process = process_old;
      client_options = old_client_options;
      server_options = old_server_options;
      if (sock == -1)
        sock = OLD_TELNET;
      break;
    case 'p':
      sock = atoi (optarg);
      break;
    case 's':
      if (telnet != NULL)
        usage (argv[0], 1);
      telnet = telnet_server;
      break;
    default:
      usage (argv[0], 1);
    }
  }

  /* These are the defaults. */
  if (sock == -1)
    sock = NEW_TELNET;
  if (process == NULL)
    process = process_new;
  if (telnet == NULL)
    telnet = telnet_client;
  if (client_options == NULL)
    client_options = new_client_options;
  if (server_options == NULL)
    server_options = new_server_options;

  if (telnet == telnet_client)
    host = atoi (argv[optind++]);

  if (argc != optind)
    usage(argv[0], 1);

  if (ncp_init (NULL) == -1) {
    fprintf (stderr, "NCP initialization error: %s.\n", strerror (errno));
    if (errno == ECONNREFUSED)
      fprintf (stderr, "Is the NCP server started?\n");
    else if (errno == EFAULT)
      fprintf (stderr, "Is the NCP environment variable set?\n");
    exit (1);
  }

  if (telnet == telnet_client)
    options = client_options;
  else
    options = server_options;
  telnet (host, sock, process, options);

  return 0;
}
