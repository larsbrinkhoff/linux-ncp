#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include "tty.h"

#include <stdio.h>

static struct termios saved;

void tty_restore (void)
{
  tcsetattr (0, TCSAFLUSH, &saved);
}

void tty_raw (void)
{
  struct termios new;
  tcgetattr (0, &saved);
  new = saved;
  new.c_iflag = 0;
  new.c_oflag = 0;
  new.c_lflag = 0;
  new.c_cc[VMIN] = 1;
  new.c_cc[VTIME] = 0;
  tcsetattr (0, TCSAFLUSH, &new);
}

pid_t tty_run (char **cmd, int *master_fd)
{
  int fd = posix_openpt (O_RDWR);
  pid_t pid;
  if (fd < 0 ||
      grantpt (fd) < 0 ||
      unlockpt (fd) < 0) {
    exit (1);
  }

  pid = fork ();
  if (pid) {
    *master_fd = fd;
    return pid;
  }

  close (0);
  close (1);
  close (2);

  setsid ();

  if (open (ptsname (fd), O_RDWR) != 0)
    exit (1);
  close (fd);
  dup (0);
  dup (1);
  ioctl (0, TIOCSCTTY);

  execvp (cmd[0], cmd);
  exit (1);
}
