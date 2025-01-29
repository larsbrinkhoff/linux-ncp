#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>

#include "ncp.h"

static int host;
static int seq = 1;
static int count = -1;
static struct timespec interval;

static void usage (const char *argv0)
{
  fprintf (stderr, "Usage: %s [-c<count>] host [seq]\n", argv0);
  exit (1);
}

static void args (int argc, char **argv)
{
  double x;
  int c;

  interval.tv_sec = 1;
  interval.tv_nsec = 0;

  while ((c = getopt (argc, argv, "c:i:")) != -1) {
    switch (c) {
    case 'c':
      count = atoi (optarg);
      break;
    case 'i':
      x = atof (optarg);
      interval.tv_sec = (int)x;
      interval.tv_nsec = 1e9 * (x - interval.tv_sec);
      break;
    default:
      usage (argv[0]);
    }
  }

  if (optind == argc)
    usage (argv[0]);

  host = atoi (argv[optind]);
  optind++;

  if (optind < argc)
    seq = atoi (argv[optind]);
}

static int difference (struct timeval *start, struct timeval *stop)
{
  int ms;
  ms = 1000 * (stop->tv_sec - start->tv_sec);
  ms += (stop->tv_usec - start->tv_usec + 500) / 1000;
  return ms;
}

int main (int argc, char **argv)
{
  struct timeval start, stop;
  int ms, reply;

  args (argc, argv);

  if (ncp_init (NULL) == -1) {
    fprintf (stderr, "NCP initialization error: %s.\n", strerror (errno));
    if (errno == ECONNREFUSED)
      fprintf (stderr, "Is the NCP server started?\n");
    else if (errno == EFAULT)
      fprintf (stderr, "Is the NCP environment variable set?\n");
    exit (1);
  }

  printf ("NCP PING host %03o\n", host);

  while (count != 0) {
    gettimeofday (&start, NULL);
    switch (ncp_echo (host, seq, &reply)) {
    case 0:
      break;
    case -2:
      fprintf (stderr, "IMP cannot be reached.\n");
      exit (1);
      break;
    case -3:
      fprintf (stderr, "Host is not up.\n");
      exit (1);
      break;
    case -5:
      fprintf (stderr, "Communication administratively prohibited.\n");
      exit (1);
      break;
    default:
      fprintf (stderr, "NCP echo error.\n");
      exit (1);
    }
    gettimeofday (&stop, NULL);
    ms = difference (&start, &stop);
    printf ("Reply from host %03o: seq=%u time=%ums\n", host, reply, ms);
    count--;
    if (count != 0)
      nanosleep (&interval, NULL);
    seq++;
  }
}
