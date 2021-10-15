/* Daemon implementing the ARPANET NCP.  Talks to the IMP interface
   and applications. */

#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/select.h>

#include "imp.h"
#include "wire.h"

#define IMP_REGULAR       0
#define IMP_LEADER_ERROR  1
#define IMP_DOWN          2
#define IMP_BLOCKED       3
#define IMP_NOP           4
#define IMP_RFNM          5
#define IMP_FULL          6
#define IMP_DEAD          7
#define IMP_DATA_ERROR    8
#define IMP_INCOMPL       9
#define IMP_RESET        10

#define NCP_NOP      0
#define NCP_RTS      1
#define NCP_STR      2
#define NCP_CLS      3
#define NCP_ALL      4
#define NCP_GVB      5
#define NCP_RET      6
#define NCP_INR      7
#define NCP_INS      8
#define NCP_ECO      9
#define NCP_ERP     10
#define NCP_ERR     11
#define NCP_RST     12
#define NCP_RRP     13
#define NCP_MAX     NCP_RRP

#define CONNECTIONS 20

static int fd;
static struct sockaddr_un server;
static struct sockaddr_un client;
static socklen_t len;

struct
{
  struct sockaddr_un client;
  socklen_t len;
  int host;
  int link1, size1, lsock1, rsock1; // Our receive link.
  int link2, size2, lsock2, rsock2; // Our send link.
} connection[CONNECTIONS];

struct
{
  struct sockaddr_un client;
  socklen_t len;
  int sock;
} listening[CONNECTIONS];

static const char *type_name[] =
{
  "NOP", // 0
  "RTS", // 1
  "STR", // 2
  "CLS", // 3
  "ALL", // 4
  "GVB", // 5
  "RET", // 6
  "INR", // 7
  "INS", // 8
  "ECO", // 9
  "ERP", // 10
  "ERR", // 11
  "RST", // 12
  "RRP"  // 13
};

static uint8_t packet[200];

static int find_link (int host, int link)
{
  int i;
  for (i = 0; i < CONNECTIONS; i++) {
    if (connection[i].host == host && connection[i].link1 == link)
      return i;
    if (connection[i].host == host && connection[i].link2 == link)
      return i;
  }
  return -1;
}

static int find_sockets (int host, uint32_t lsock, uint32_t rsock)
{
  int i;
  for (i = 0; i < CONNECTIONS; i++) {
    if (connection[i].host == host && connection[i].lsock1 == lsock
        && connection[i].rsock1 == rsock)
      return i;
    if (connection[i].host == host && connection[i].lsock2 == lsock
        && connection[i].rsock2 == rsock)
      return i;
  }
  return -1;
}

static int find_listen (int socket)
{
  int i;
  for (i = 0; i < CONNECTIONS; i++) {
    if (listening[i].sock == socket)
      return i;
  }
  return -1;
}


static void destroy (int i)
{
  connection[i].host = connection[i].link1 = connection[i].link2 =
    connection[i].size1 = connection[i].size2 =
    connection[i].lsock1 = connection[i].rsock1 =
    connection[i].lsock2 = connection[i].rsock2 = -1;
}

static void send_imp (int flags, int type, int destination, int link, int id,
                      int subtype, void *data, int words)
{
  packet[12] = flags << 4 | type;
  packet[13] = destination;
  packet[14] = link;
  packet[15] = id << 4 | subtype;
  if (data != NULL)
    memcpy (packet + 16, data, 2 * (words - 2));
  imp_send_message (packet, words);
}

static void send_leader_error (int subtype)
{
  send_imp (0, IMP_NOP, 0, 0, 0, subtype, NULL, 2);
}

static void send_nop (void)
{
  send_imp (0, IMP_NOP, 0, 0, 0, 0, NULL, 2);
}

static void send_reset (void)
{
  send_imp (0, IMP_RESET, 0, 0, 0, 0, NULL, 2);
}

static void send_ncp (uint8_t destination, uint8_t byte, uint16_t count,
                      uint8_t type)
{
  packet[16] = 0;
  packet[17] = byte;
  packet[18] = count >> 8;
  packet[19] = count;
  packet[20] = 0;
  packet[21] = type;
  fprintf (stderr, "NCP: send to %03o, type %d/%s.\n",
           destination, type, type <= NCP_MAX ? type_name[type] : "???");
  send_imp (0, IMP_REGULAR, destination, 0, 0, 0, NULL, (count + 9 + 1)/2);
}

static int make_open (int host, int lsock1, int rsock1, int lsock2, int rsock2)
{
  int i = find_link (-1, -1);
  if (i == -1) {
    fprintf (stderr, "NCP: Table full.\n");
    return -1;
  }

  connection[i].host = host;
  if (lsock1 != -1)
    connection[i].lsock1 = lsock1;
  if (rsock1 != -1)
    connection[i].rsock1 = rsock1;
  if (lsock2 != -1)
    connection[i].lsock2 = lsock2;
  if (rsock2 != -1)
    connection[i].rsock2 = rsock2;

  return i;
}

// Sender to receiver.
void ncp_str (uint8_t destination, uint32_t lsock, uint32_t rsock, uint8_t size)
{
  packet[22] = lsock >> 24;
  packet[23] = lsock >> 16;
  packet[24] = lsock >> 8;
  packet[25] = lsock;
  packet[26] = rsock >> 24;
  packet[27] = rsock >> 16;
  packet[28] = rsock >> 8;
  packet[29] = rsock;
  packet[30] = size;
  send_ncp (destination, 8, 0, NCP_STR);
}

// Receiver to sender.
void ncp_rts (uint8_t destination, uint32_t lsock, uint32_t rsock, uint8_t link)
{
  packet[22] = lsock >> 24;
  packet[23] = lsock >> 16;
  packet[24] = lsock >> 8;
  packet[25] = lsock;
  packet[26] = rsock >> 24;
  packet[27] = rsock >> 16;
  packet[28] = rsock >> 8;
  packet[29] = rsock;
  packet[30] = link;
  send_ncp (destination, 8, 0, NCP_RTS);
}

// Allocate.
void ncp_all (uint8_t destination, uint8_t link, uint16_t msg_space, uint32_t bit_space)
{
  send_ncp (destination, 8, 0, NCP_ALL);
}

// Return.
void ncp_ret (uint8_t destination, uint8_t link, uint16_t msg_space, uint32_t bit_space)
{
  send_ncp (destination, 8, 0, NCP_RET);
}

// Give back.
void ncp_gvb (uint8_t destination, uint8_t link, uint8_t fm, uint8_t fb)
{
  send_ncp (destination, 8, 0, NCP_GVB);
}

// Interrupt by receiver.
void ncp_inr (uint8_t destination, uint8_t link)
{
  send_ncp (destination, 8, 0, NCP_INR);
}

// Interrupt by sender.
void ncp_ins (uint8_t destination, uint8_t link)
{
  send_ncp (destination, 8, 0, NCP_INS);
}

// Close.
void ncp_cls (uint8_t destination, uint32_t lsock, uint32_t rsock)
{
  packet[22] = lsock >> 24;
  packet[23] = lsock >> 16;
  packet[24] = lsock >> 8;
  packet[25] = lsock;
  packet[26] = rsock >> 24;
  packet[27] = rsock >> 16;
  packet[28] = rsock >> 8;
  packet[29] = rsock;
  send_ncp (destination, 8, 0, NCP_CLS);
}

// Echo.
void ncp_eco (uint8_t destination, uint8_t data)
{
  memset (packet, 0, sizeof packet);
  packet[22] = data;
  send_ncp (destination, 8, 2, NCP_ECO);
}

// Echo reply.
void ncp_erp (uint8_t destination, uint8_t data)
{
  packet[22] = data;
  send_ncp (destination, 8, 2, NCP_ERP);
}

// Reset.
void ncp_rst (uint8_t destination)
{
  send_ncp (destination, 8, 1, NCP_RST);
}

// Reset reply.
void ncp_rrp (uint8_t destination)
{
  send_ncp (destination, 8, 1, NCP_RRP);
}

// No operation.
void ncp_nop (uint8_t destination)
{
  send_ncp (destination, 8, 1, NCP_NOP);
}

// Error.
void ncp_err (uint8_t destination, uint8_t code, void *data, int length)
{
  memcpy (packet + 22, data, length);
  send_ncp (destination, 8, length + 1, NCP_ERR);
}

static int process_nop (uint8_t source, uint8_t *data)
{
  return 0;
}

static void reply_open (uint8_t host, uint8_t socket, uint8_t connection)
{
  uint8_t reply[4];
  reply[0] = WIRE_OPEN+1;
  reply[1] = host;
  reply[2] = socket;
  reply[3] = connection;
  if (sendto (fd, reply, sizeof reply, 0, (struct sockaddr *)&client, len) == -1)
    fprintf (stderr, "NCP: sendto %s error: %s.\n",
             client.sun_path, strerror (errno));
}

static void reply_listen (uint8_t host, uint8_t socket, uint8_t connection)
{
  uint8_t reply[4];
  reply[0] = WIRE_LISTEN+1;
  reply[1] = host;
  reply[2] = socket;
  reply[3] = connection;
  if (sendto (fd, reply, sizeof reply, 0, (struct sockaddr *)&client, len) == -1)
    fprintf (stderr, "NCP: sendto %s error: %s.\n",
             client.sun_path, strerror (errno));
}

static void reply_close (uint8_t connection)
{
  uint8_t reply[2];
  reply[0] = WIRE_CLOSE+1;
  reply[1] = connection;
  if (sendto (fd, reply, sizeof reply, 0, (struct sockaddr *)&client, len) == -1)
    fprintf (stderr, "NCP: sendto %s error: %s.\n",
             client.sun_path, strerror (errno));
}

static int process_rts (uint8_t source, uint8_t *data)
{
  int i;
  uint32_t lsock, rsock;
  rsock = (data[0] << 24) | (data[1] << 24) | (data[2] << 24) | data[3];
  lsock = (data[4] << 24) | (data[5] << 24) | (data[6] << 24) | data[7];

  fprintf (stderr, "NCP: recieved STR %u:%u from %03o.\n",
           lsock, rsock, source);

  if (find_listen (rsock) == -1) {
    i = find_sockets (data[0], lsock, rsock);
  } else {
    i = find_sockets (data[0], lsock, rsock);
    if (i == -1)
      i = make_open (data[0], lsock, rsock, -1, -1);
  }
  connection[i].link2 = data[8]; //Send link.
  if (connection[i].size2 == -1) {
    connection[i].size2 = 8; //Send byte size.
    ncp_str (connection[i].host, lsock, rsock, connection[i].size2);
    if (connection[i].link1 != -1)
      reply_listen (source, rsock-1, i);
  } else {
    if (connection[i].size1 != -1)
      reply_open (source, rsock-1, i);
  }

  return 9;
}

static int process_str (uint8_t source, uint8_t *data)
{
  int i;
  uint32_t lsock, rsock;
  rsock = (data[0] << 24) | (data[1] << 24) | (data[2] << 24) | data[3];
  lsock = (data[4] << 24) | (data[5] << 24) | (data[6] << 24) | data[6];

  fprintf (stderr, "NCP: recieved STR %u:%u from %03o.\n",
           lsock, rsock, source);

  if (find_listen (rsock) == -1) {
    i = find_sockets (data[0], lsock, rsock);
  } else {
    i = find_sockets (data[0], lsock, rsock);
    if (i == -1)
      i = make_open (data[0], -1, -1, lsock, rsock);
  }
  connection[i].size1 = data[7]; //Receive byte size.
  if (connection[i].link1 == -1) {
    connection[i].link1 = 42; //Receive link.
    ncp_rts (connection[i].host, lsock, rsock, connection[i].link1);
    if (connection[i].size2 != -1)
      reply_listen (source, rsock, i);
  } else {
    if (connection[i].link2 != -1)
      reply_open (source, rsock, i);
  }

  return 9;
}

static int process_cls (uint8_t source, uint8_t *data)
{
  int i;
  uint32_t lsock, rsock;

  lsock = (data[1] << 24) | (data[2] << 24) | (data[3] << 24) | data[4];
  rsock = (data[5] << 24) | (data[6] << 24) | (data[7] << 24) | data[8];
  i = find_sockets (data[0], lsock, rsock);
  if (connection[i].lsock1 == lsock)
    connection[i].lsock1 = connection[i].rsock1 = -1;
  if (connection[i].lsock2 == lsock)
    connection[i].lsock2 = connection[i].rsock2 = -1;

  if (connection[i].size1 == -1) {
    // Remote confirmed closing.
    if (connection[i].lsock1 == -1 && connection[i].lsock2 == -1) {
      destroy (i);
      reply_close (i);
    }
  } else {
    // Remote closed connection.
    ncp_cls (connection[i].host, lsock, rsock);
    if (connection[i].lsock1 == -1 && connection[i].lsock2 == -1)
      destroy (i);
  }

  return 8;
}

static int process_all (uint8_t source, uint8_t *data)
{
  return 9;
}

static int process_gvb (uint8_t source, uint8_t *data)
{
  return 3;
}

static int process_ret (uint8_t source, uint8_t *data)
{
  return 7;
}

static int process_inr (uint8_t source, uint8_t *data)
{
  return 1;
}

static int process_ins (uint8_t source, uint8_t *data)
{
  return 1;
}

static int process_eco (uint8_t source, uint8_t *data)
{
  fprintf (stderr, "NCP: recieved ECO %03o from %03o, replying ERP %03o.\n",
           *data, source, *data);
  ncp_erp (source, *data);
  return 1;
}

static int process_erp (uint8_t source, uint8_t *data)
{
  uint8_t reply[3];
  fprintf (stderr, "NCP: recieved ERP %03o from %03o.\n",
           *data, source);
  reply[0] = WIRE_ECHO+1;
  reply[1] = source;
  reply[2] = *data;
  if (sendto (fd, reply, sizeof reply, 0, (struct sockaddr *)&client, len) == -1)
    fprintf (stderr, "NCP: sendto %s error: %s.\n",
             client.sun_path, strerror (errno));
  return 1;
}

static int process_err (uint8_t source, uint8_t *data)
{
  fprintf (stderr, "NCP: recieved ERR code %03o from %03o.\n", *data, source);
  return 11;
}

static int process_rst (uint8_t source, uint8_t *data)
{
  fprintf (stderr, "NCP: recieved RST from %03o.\n", source);
  ncp_rrp (source);
  return 0;
}

static int process_rrp (uint8_t source, uint8_t *data)
{
  fprintf (stderr, "NCP: recieved RRP from %03o.\n", source);
  return 0;
}

static int (*ncp_messages[]) (uint8_t source, uint8_t *data) =
{
  process_nop,
  process_rts,
  process_str,
  process_cls,
  process_all,
  process_gvb,
  process_ret,
  process_inr,
  process_ins,
  process_eco,
  process_erp,
  process_err,
  process_rst,
  process_rrp
};

static void process_ncp (uint8_t source, uint8_t *data, uint16_t count)
{
  int i = 0;
  while (i < count) {
    uint8_t type = data[i++];
    if (type > NCP_MAX) {
      ncp_err (source, 1, data - 1, 10);
      return;
    }
    fprintf (stderr, "NCP: process type %d/%s from %03o\n",
             type, type_name[type], source);
    i += ncp_messages[type] (source, &data[i]);
  }
}

static void process_regular (uint8_t *packet, int length)
{
  uint8_t source = packet[1];
  uint8_t link = packet[2];

  if (link == 0) {
    uint16_t count = (packet[6] << 8) | packet[7];
    process_ncp (source, &packet[9], count);
  } else {
    fprintf (stderr, "NCP: process regular from %03o link %u.\n",
             source, link);
  }
}

static void process_leader_error (uint8_t *packet, int length)
{
  const char *reason;
  switch (packet[3] & 0x0F) {
  case 0: reason = "IMP error during leader"; break;
  case 1: reason = "Message less than 32 bits"; break;
  case 2: reason = "Illegal type"; break;
  default: reason = "Unknown reason"; break;
  }
  fprintf (stderr, "NCP: Error in leader: %s.\n", reason);
}

static void process_imp_down (uint8_t *packet, int length)
{
  fprintf (stderr, "NCP: IMP going down.\n");
}

static void process_blocked (uint8_t *packet, int length)
{
  fprintf (stderr, "NCP: Blocked link.\n");
}

static void process_imp_nop (uint8_t *packet, int length)
{
  fprintf (stderr, "NCP: NOP.\n");
}

static void process_rfnm (uint8_t *packet, int length)
{
  fprintf (stderr, "NCP: Ready for next message to host %03o link %u.\n",
           packet[1], packet[2]);
}

static void process_full (uint8_t *packet, int length)
{
  fprintf (stderr, "NCP: Link table full.\n");
}

static void process_host_dead (uint8_t *packet, int length)
{
  const char *reason;
  switch (packet[3] & 0x0F) {
  case 0: reason = "IMP cannot be reached"; break;
  case 1: reason = "host is not up"; break;
  case 3: reason = "host communication administratively prohibited"; break;
  default: reason = "dead, unknown reason"; break;
  }
  fprintf (stderr, "NCP: Destination %03o %s.\n", packet[1], reason);
}

static void process_data_error (uint8_t *packet, int length)
{
  fprintf (stderr, "NCP: Error in data.\n");
}

static void process_incomplete (uint8_t *packet, int length)
{
  const char *reason;
  switch (packet[3] & 0x0F) {
  case 0: reason = "Host did not accept message quickly enough"; break;
  case 1: reason = "Message too long"; break;
  case 2: reason = "Message took too long in transmission"; break;
  case 3: reason = "Message lost in network"; break;
  case 4: reason = "Resources unavailable"; break;
  case 5: reason = "I/O failure during reception"; break;
  default: reason = "Unknown reason"; break;
  }
  fprintf (stderr, "NCP: Incomplete transmission from %03o: %s.\n",
           packet[1], reason);
}

static void process_reset (uint8_t *packet, int length)
{
  fprintf (stderr, "NCP: IMP reset.\n");
}

static void (*imp_messages[]) (uint8_t *packet, int length) =
{
  process_regular,
  process_leader_error,
  process_imp_down,
  process_blocked,
  process_imp_nop,
  process_rfnm,
  process_full,
  process_host_dead,
  process_data_error,
  process_incomplete,
  process_reset
};

static void process_imp (uint8_t *packet, int length)
{
  int type;
  if (length < 2) {
    send_leader_error (1);
    return;
  }
  type = packet[0] & 0x0F;
  if (type <= IMP_RESET)
    imp_messages[type] (packet, length);
  else
    send_leader_error (2);
}

static void send_nops (void)
{
  send_nop ();
  sleep (1);
  send_nop ();
  sleep (1);
  send_nop ();
}

static void ncp_reset (int flap)
{
  fprintf (stderr, "NCP: Reset.\n");
  if (flap) {
    fprintf (stderr, "NCP: Flap host ready.\n");
    imp_host_ready (0);
    imp_host_ready (1);
  }

  send_nops ();
}

static int imp_ready = 0;

static void ncp_imp_ready (int flag)
{
  if (!imp_ready && flag) {
    fprintf (stderr, "NCP: IMP going up.\n");
    //ncp_reset (0);
  } else if (imp_ready && !flag) {
    fprintf (stderr, "NCP: IMP going down.\n");
  }
  imp_ready = flag;
}

static void app_echo (void)
{
  fprintf (stderr, "NCP: Application echo.\n");
  ncp_eco (packet[1], packet[2]);
}

static void app_open (void)
{
  int i;

  fprintf (stderr, "NCP: Application open.\n");

  i = make_open (packet[1], 0123, packet[2], 0124, packet[2]+1);
  connection[i].link1 = 42; //Receive link.
  connection[i].size2 = 8;  //Send byte size.

  ncp_rts (connection[i].host, connection[i].lsock1, connection[i].rsock1,
           connection[i].link1);
  ncp_str (connection[i].host, connection[i].lsock2, connection[i].rsock2,
           connection[i].size2);
}

static void app_listen (void)
{
  int i;
  fprintf (stderr, "NCP: Application listen.\n");
  i = find_listen (-1);
  if (i == -1) {
    fprintf (stderr, "NCP: Table full.\n");
    return;
  }
  listening[i].sock = packet[1];
}

static void app_read (void)
{
  fprintf (stderr, "NCP: Application read.\n");
}

static void app_write (void)
{
  fprintf (stderr, "NCP: Application write.\n");
}

static void app_close (void)
{
  int i = packet[1];
  fprintf (stderr, "NCP: Application close.\n");
  connection[i].size1 = connection[i].size2 = -1;
  ncp_cls (packet[1], connection[i].lsock1, connection[i].rsock1);
  ncp_cls (packet[1], connection[i].lsock2, connection[i].rsock2);
}

static void application (void)
{
  ssize_t n;

  len = sizeof client;
  n = recvfrom (fd, packet, sizeof packet, 0, (struct sockaddr *)&client, &len);
  if (n == -1) {
    fprintf (stderr, "NCP: recvfrom error.\n");
    return;
  }

  fprintf (stderr, "NCP: Received application request %u from %s.\n",
           packet[0], client.sun_path);

  if (!wire_check (packet[0], n)) {
    fprintf (stderr, "NCP: bad application request.\n");
    return;
  }

  switch (packet[0]) {
  case WIRE_ECHO:   app_echo (); break;
  case WIRE_OPEN:   app_open (); break;
  case WIRE_LISTEN: app_listen (); break;
  case WIRE_READ:   app_read (); break;
  case WIRE_WRITE:  app_write (); break;
  case WIRE_CLOSE:  app_close (); break;
  default:          fprintf (stderr, "NCP: bad application request.\n"); break;
  }
}

static void cleanup (void)
{
  unlink (server.sun_path);
}

void ncp_init (void)
{
  char *path;
  int i;

  fd = socket (AF_UNIX, SOCK_DGRAM, 0);
  memset (&server, 0, sizeof server);
  server.sun_family = AF_UNIX;
  path = getenv ("NCP");
  strncpy (server.sun_path, path, sizeof server.sun_path - 1);
  if (bind (fd, (struct sockaddr *)&server, sizeof server) == -1) {
    fprintf (stderr, "NCP: bind error: %s.\n", strerror (errno));
    exit (1);
  }
  atexit (cleanup);

  for (i = 0; i < CONNECTIONS; i ++) {
    destroy (i);
    listening[i].sock = -1;
  }
}

int main (int argc, char **argv)
{
  imp_init (argc, argv);
  ncp_init ();
  imp_imp_ready = ncp_imp_ready;
  imp_host_ready (1);
  ncp_reset (0);
  for (;;) {
    int n;
    fd_set rfds;
    FD_ZERO (&rfds);
    FD_SET (fd, &rfds);
    imp_fd_set (&rfds);
    n = select (33, &rfds, NULL, NULL, NULL);
    if (n == -1)
      fprintf (stderr, "NCP: select error.\n");
    else if (n > 0) {
      if (imp_fd_isset (&rfds)) {
        memset (packet, 0, sizeof packet);
        imp_receive_message (packet, &n);
        process_imp (packet, n);
      }
      if (FD_ISSET (fd, &rfds)) {
        application ();
      }
    }
  }
}
