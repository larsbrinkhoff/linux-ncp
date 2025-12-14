void tty_raw (void);
void tty_restore (void);
/*
 * tty_run - run command in a pseudo-terminal.
 * @cmd: command to run.
 * @master_fd: returns the master file descriptor.
 * returns: pid of the spawned process.
 */
pid_t tty_run (char **cmd, int *master_fd);
