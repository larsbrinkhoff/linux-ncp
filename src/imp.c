/* Interface between NCP and IMP. */

#include <stdio.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "imp.h"

static int imp_sock;
static int port;
static struct sockaddr_in destination;
static uint32_t rx_sequence, tx_sequence;

static const char *type_name[] =
{
  "REGULAR",  // 0
  "ERROR",    // 1
  "DOWN",     // 2
  "BLOCKED",  // 3
  "NOP",      // 4
  "RFNM",     // 5
  "FULL",     // 6
  "DEAD",     // 7
  "ERROR_ID", // 8
  "INCOMPL",  // 9
  "RESET",    //10
  "???",      //11
  "???",      //12
  "???",      //13
  "???",      //14
  "NEW",      //15
};

static void fatal (const char *message)
{
  fprintf (stderr, "Fatal error: %s\n", message);
  exit (1);
}

static void args (int argc, char **argv)
{
  struct hostent *h;

  if (argc != 4)
    fatal ("args");

  h = gethostbyname (argv[1]);
  if (h == NULL)
    fatal ("gethostbyname");

  destination.sin_family = AF_INET;
  destination.sin_port = htons (atoi (argv[2]));
  memcpy (&destination.sin_addr, h->h_addr, h->h_length);

  port = htons (atoi (argv[3]));
}

static void make_socket (void)
{
  struct sockaddr_in source;

  imp_sock = socket (AF_INET, SOCK_DGRAM, 0);
  if (imp_sock == -1)
    fatal ("socket");

  source.sin_family = AF_INET;
  source.sin_addr.s_addr = INADDR_ANY;
  source.sin_port = port;
  if (bind (imp_sock, (struct sockaddr *)&source, sizeof source) == -1)
    fatal ("bind");
}

void imp_send_message (uint8_t *data, int length)
{
  int r;

  data[0] = 'H';
  data[1] = '3';
  data[2] = '1';
  data[3] = '6';
  data[4] = tx_sequence >> 24;
  data[5] = tx_sequence >> 16;
  data[6] = tx_sequence >> 8;
  data[7] = tx_sequence;
  data[8] = ++length >> 8;
  data[9] = length;
  data[10] = 0;
  data[11] = 1;

  r = sendto (imp_sock, data, 2 * length + 10, 0,
              (struct sockaddr *)&destination, sizeof destination);
  if (r == -1)
    fprintf (stderr, "Send error: %s\n", strerror (errno));
  fprintf (stderr, "Send message #%u: type %d/%s, destination %03o, %d words.\n",
           tx_sequence, data[12] & 0x0F, type_name[data[12] & 0x0F],
           data[13], length - 1);
  tx_sequence++;
}

static uint8_t message[200];

void imp_receive_message (uint8_t *data, int *length)
{
  uint32_t x;
  int n;

  *length = 0;

  n = read (imp_sock, message, sizeof message);
  if (n == 0)
    return;
  else if (n == -1) {
    fprintf (stderr, "Receive error: %s\n", strerror (errno));
    return;
  }

  if (message[0] != 'H' ||
      message[1] != '3' ||
      message[2] != '1' ||
      message[3] != '6') {
    int i;
    fprintf (stderr, "Receive error: bad magic.\n");
    for (i = 0; i < n; i++)
      fprintf (stderr, "%02X ", message[i]);
    return;
  }

  x = (message[4] << 24) | (message[5] << 16) | (message[6] << 8) | message[7];
  if (x == 0 && rx_sequence != 0) {
    fprintf (stderr, "Receive: IMP restarted.\n");
    rx_sequence = x;
  } else if (x < rx_sequence) {
    fprintf (stderr, "Receive error: bad sequence number: %u.\n", x);
    *length = 0;
    return;
  } else if (x != rx_sequence) {
    fprintf (stderr, "Receive error: out of sequence: %u.\n", x);
    rx_sequence = x;
  }

  *length = message[8] << 8 | message[9];
  if (n != 2 * *length + 10)
    fprintf (stderr, "Receive error: bad length.\n");

  memcpy (data, message + 12, n - 12);

  fprintf (stderr, "Receive message #%u: type %d/%s, source %03o, %d words.\n",
           rx_sequence, message[12] & 0x0F, type_name[message[12] & 0x0F],
           message[13], *length - 1);
  if (message[12] & 0x0F)
    *length = 0;

  rx_sequence++;
}

void imp_init (int argc, char **argv)
{
  args (argc, argv);
  make_socket ();
  rx_sequence = tx_sequence = 0;
}
