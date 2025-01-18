/* Library for applications. */

extern int ncp_init (const char *path);
extern int ncp_echo (int host, int data, int *reply);
extern int ncp_open (int host, unsigned socket, int *size, int *connection);
extern int ncp_listen (unsigned socket, int *size, int *host, int *connection);
extern int ncp_read (int connection, void *data, int *length);
extern int ncp_write (int connection, void *data, int *length);
extern int ncp_interrupt (int connection);
extern int ncp_close (int connection);
