#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "inet.h"

static void fatal (const char *message, int error)
{
  fprintf (stderr, "Fatal error %s\n", message);
  if (error)
    fprintf (stderr, ": %s", strerror (error));
  fputc ('\n', stderr);
  exit (1);
}

int do_the_thing (const char *host, const char *port,
                  int (*the_thing) (int, const struct sockaddr *, socklen_t))
{
  struct addrinfo hints;
  struct addrinfo *addr;
  struct addrinfo *rp;
  int fd, reuse = 1;

  memset (&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if (getaddrinfo (host, port, &hints, &addr) != 0)
    fatal ("calling getaddrinfo", errno);

  for (rp = addr; rp != NULL; rp = rp->ai_next) {
    fd = socket (rp->ai_family, rp->ai_socktype, 0);
    if (fd < 0)
      fatal ("creating socket", errno);

    if (setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse) < 0)
      fatal ("calling setsockopt", errno);

    if (the_thing (fd, rp->ai_addr, rp->ai_addrlen) == 0)
      goto ok;
    fprintf (stderr, "Thing: %s\n", strerror (errno));
  }
  fatal ("do the thing to socket", 0);

 ok:
  freeaddrinfo (addr);
  return fd;
}

int inet_server (const char *port)
{
  int fd = do_the_thing (NULL, port, bind);
  if (listen (fd, 1) < 0)
    fatal ("binding socket", errno);
  return fd;
}

int inet_accept (int s, char **host, int *port)
{
  struct sockaddr addr;
  socklen_t len = sizeof addr;
  int fd = accept (s, &addr, &len);
  if (fd < 0)
    fatal ("calling accept", errno);
  if (addr.sa_family == AF_INET) {
    struct sockaddr_in *x = (struct sockaddr_in *)&addr;
    *host = inet_ntoa (x->sin_addr);
    *port = ntohs (x->sin_port);
  } else {
    *host = "(unknown)";
    *port = 0;
  }
  return fd;
}

int inet_connect (const char *host, const char *port)
{
  return do_the_thing (host, port, connect);
}
