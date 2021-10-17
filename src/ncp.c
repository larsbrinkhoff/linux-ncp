/* Daemon implementing the ARPANET NCP.  Talks to the IMP interface
   and applications. */

#include <stdint.h>
#include <unistd.h>
#include <string.h>

#include "imp.h"

#define IMP_REGULAR   0
#define IMP_ERROR     1
#define IMP_DOWN      2
#define IMP_BLOCKED   3
#define IMP_NOP       4
#define IMP_RFNM      5
#define IMP_FULL      6
#define IMP_DEAD      7
#define IMP_ERROR_ID  8
#define IMP_INCOMPL   9
#define IMP_RESET    10

static uint8_t packet[200];

static void send_imp (int flags, int type, int destination, int id,
                      int subtype, void *data, int length)
{
  memset (packet, 0, sizeof packet);
  packet[12] = flags << 4 | type;
  packet[13] = destination;
  packet[14] = id >> 4;
  packet[15] = id << 4 | subtype;
  if (length > 0)
    memcpy (packet + 16, data, length);
  imp_send_message (packet, length + 2);
}

static void send_nop (void)
{
  send_imp (0, IMP_NOP, 0, 0, 0, NULL, 0);
}

static void send_rfnm (void)
{
  send_imp (0, IMP_RFNM, 0, 0, 0, NULL, 0);
}

static void send_reset (void)
{
  send_imp (0, IMP_RESET, 0, 0, 0, NULL, 0);
}

static void send_nops (void)
{
  send_nop ();
  sleep (1);
  send_nop ();
  sleep (1);
  send_nop ();
  sleep (1);
}

int main (int argc, char **argv)
{
  int n;
  imp_init (argc, argv);
  send_nops ();
  for (;;)
    imp_receive_message (packet, &n);
}
