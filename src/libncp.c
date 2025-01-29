/* Library for applications. */

#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "ncp.h"
#include "wire.h"

static int fd;
static struct sockaddr_un addr;
static uint8_t message[1000];

static void cleanup (void)
{
  close (fd);
  unlink (addr.sun_path);
}

static void quit (int x)
{
  exit (0);
}

int ncp_init (const char *path)
{
  struct sockaddr_un server;

  fd = socket (AF_UNIX, SOCK_DGRAM, 0);
  if (fd == -1)
    return -1;
  memset (&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  snprintf (addr.sun_path, sizeof addr.sun_path - 1,
            "/tmp/client.%u", getpid ());
  unlink (addr.sun_path);
  if (bind (fd, (struct sockaddr *)&addr, sizeof addr) == -1)
    return -1;
  atexit (cleanup);
  signal (SIGINT, quit);
  signal (SIGTERM, quit);
  signal (SIGQUIT, quit);

  memset (&server, 0, sizeof server);
  server.sun_family = AF_UNIX;
  if (path == NULL)
    path = getenv ("NCP");
  errno = EFAULT;
  if (path == NULL)
    return -1;
  strncpy (server.sun_path, path, sizeof server.sun_path - 1);
  if (connect (fd, (struct sockaddr *) &server, sizeof server) == -1)
    return -1;

  return 0;
}

static int size;

static void type (uint8_t x)
{
  message[0] = x;
  size = 1;
}

static void add (uint8_t x)
{
  message[size++] = x;
}

static int transact (void)
{
  int type = message[0];
  ssize_t n;
  if (!wire_check (type, size))
    return -1;
  if (send (fd, message, size, 0) != size)
    return -1;
  n = recv (fd, message, sizeof message, 0);
  if (message[0] != type + 1)
    return -1;
  if (!wire_check (message[0], n))
    return -1;
  return n;
}

int ncp_echo (int host, int data, int *reply)
{
  type (WIRE_ECHO);
  add (host);
  add (data);
  if (transact () == -1)
    return -1;
  if (message[1] != host)
    return -1;
  *reply = message[2];
  if (message[3] == 0x10)
    return 0;
  else
    return -message[3] - 2;
}

static int u32 (uint8_t *data)
{
  return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

int ncp_open (int host, unsigned socket, int *size, int *connection)
{
  type (WIRE_OPEN);
  add (host);
  add (socket >> 24);
  add (socket >> 16);
  add (socket >> 8);
  add (socket);
  add (*size);
  if (transact () == -1)
    return -1;
  if (message[1] != host)
    return -1;
  if (u32 (message + 2) != socket)
    return -1;
  if (message[8] == 255)
    return -2;
  *connection = message[6];
  *size = message[7];
  return 0;
}

int ncp_listen (unsigned socket, int *size, int *host, int *connection)
{
  type (WIRE_LISTEN);
  add (socket >> 24);
  add (socket >> 16);
  add (socket >> 8);
  add (socket);
  add (*size);
  if (transact () == -1)
    return -1;
  if (message[1] == 0)
    return -1;
  if (u32 (message + 2) != socket)
    return -1;
  *host = message[1];
  *connection = message[6];
  *size = message[7];
  return 0;
}

int ncp_read (int connection, void *data, int *length)
{
  ssize_t n;
  type (WIRE_READ);
  add (connection);
  add (*length);
  *length = 0;
  n = transact ();
  if (n == -1)
    return -1;
  if (message[1] != connection)
    return -1;
  memcpy (data, message + 2, n - 2);
  *length = n - 2;
  return 0;
}

int ncp_write (int connection, void *data, int *length)
{
  type (WIRE_WRITE);
  add (connection);
  memcpy (message + size, data, *length);
  size += *length;
  *length = 0;
  if (transact () == -1)
    return -1;
  if (message[1] != connection)
    return -1;
  *length = message[2] << 8 | message[3];
  return 0;
}

int ncp_interrupt (int connection)
{
  type (WIRE_INTERRUPT);
  add (connection);
  if (transact () == -1)
    return -1;
  if (message[1] != connection)
    return -1;
  return 0;
}

int ncp_close (int connection)
{
  type (WIRE_CLOSE);
  add (connection);
  if (transact () == -1)
    return -1;
  if (message[1] != connection)
    return -1;
  return 0;
}
