/* Daemon implementing the ARPANET NCP.  Talks to the IMP interface
   and applications. */

#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/select.h>

#include "imp.h"
#include "wire.h"

#define RFNM_TIMEOUT   10
#define RRP_TIMEOUT    20
#define ERP_TIMEOUT    20

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

#define LINK_CTL     0
#define LINK_MIN     2
#define LINK_MAX    71
#define LINK_IP    155

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

#define ERR_UNDEFINED   0 //Undefined.
#define ERR_OPCODE      1 //Illegal opcode.
#define ERR_SHORT       2 //Short parameter space.
#define ERR_PARAM       3 //Bad parameters.
#define ERR_SOCKET      4 //Request on a non-existent socket.
#define ERR_CONNECT     5 //Socket (link) not connected.

#define CONN_CLIENT        000001
#define CONN_SERVER        000002
#define CONN_SENT_RTS      000010
#define CONN_SENT_STR      000020
#define CONN_GOT_RTS       000100
#define CONN_GOT_STR       000200
#define CONN_GOT_SOCKET    000400
#define CONN_GOT_BOTH      (CONN_SERVER | CONN_GOT_RTS | CONN_GOT_STR)
#define CONN_GOT_ALL       (CONN_GOT_RTS | CONN_GOT_STR | CONN_GOT_SOCKET)

#define CONNECTIONS 20

static int fd;
static struct sockaddr_un server;
static struct sockaddr_un client;
static socklen_t len;
static unsigned long time_tick;

typedef struct
{ 
  struct sockaddr_un addr;
  socklen_t len;
} client_t;

struct
{
  client_t client;
  int host;
  unsigned flags;
  int listen;
  int all_msgs, all_bits;
  struct { int link, size; uint32_t lsock, rsock; } rcv, snd;
  void (*rrp_callback) (int);
  void (*rrp_timeout) (int);
  unsigned long rrp_time;
  void (*rfnm_callback) (int);
  void (*rfnm_timeout) (int);
  unsigned long rfnm_time;
} connection[CONNECTIONS];

struct
{
  client_t client;
  uint32_t sock;
} listening[CONNECTIONS];

// Notes which hosts are considered alive.
static struct
{
  unsigned flags;
#define HOST_ALIVE   0001

  client_t echo;
  unsigned long erp_time;
  int outstanding_rfnm;
} hosts[256];

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
static uint8_t app[200];

static void when_rrp (int i, void (*cb) (int), void (*to) (int))
{
  connection[i].rrp_callback = cb;
  connection[i].rrp_timeout = to;
  connection[i].rrp_time = time_tick + RRP_TIMEOUT;
}

static void check_rrp (int host)
{
  void (*cb) (int);
  int i;
  for (i = 0; i < CONNECTIONS; i++) {
    if (connection[i].host != host)
      continue;
    cb = connection[i].rrp_callback;
    if (cb == NULL)
      continue;
    connection[i].rrp_callback = NULL;
    connection[i].rrp_timeout = NULL;
    cb (i);
  }
}

static void when_rfnm (int i, void (*cb) (int), void (*to) (int))
{
  connection[i].rfnm_callback = cb;
  connection[i].rfnm_timeout = to;
  connection[i].rfnm_time = time_tick + RFNM_TIMEOUT;
}

static void check_rfnm (int host)
{
  void (*cb) (int);
  int i;
  for (i = 0; i < CONNECTIONS; i++) {
    if (connection[i].host != host)
      continue;
    if (connection[i].rfnm_callback == NULL)
      continue;
    if (hosts[connection[i].host].outstanding_rfnm >= 4)
      continue;
    cb = connection[i].rfnm_callback;
    connection[i].rfnm_callback = NULL;
    connection[i].rfnm_timeout = NULL;
    cb (i);
  }
}

static int find_link (int host, int link)
{
  int i;
  for (i = 0; i < CONNECTIONS; i++) {
    if (connection[i].host == host && connection[i].rcv.link == link)
      return i;
    if (connection[i].host == host && connection[i].snd.link == link)
      return i;
  }
  return -1;
}

static int find_sockets (int host, uint32_t lsock, uint32_t rsock)
{
  int i;
  for (i = 0; i < CONNECTIONS; i++) {
    if (connection[i].host == host && connection[i].rcv.lsock == lsock
        && connection[i].rcv.rsock == rsock)
      return i;
    if (connection[i].host == host && connection[i].snd.lsock == lsock
        && connection[i].snd.rsock == rsock)
      return i;
  }
  return -1;
}

static int find_rcv_sockets (int host, uint32_t lsock, uint32_t rsock)
{
  int i;
  for (i = 0; i < CONNECTIONS; i++) {
    if (connection[i].host == host && connection[i].rcv.lsock == lsock
        && connection[i].rcv.rsock == rsock)
      return i;
  }
  return -1;
}

static int find_snd_sockets (int host, uint32_t lsock, uint32_t rsock)
{
  int i;
  for (i = 0; i < CONNECTIONS; i++) {
    if (connection[i].host == host && connection[i].snd.lsock == lsock
        && connection[i].snd.rsock == rsock)
      return i;
  }
  return -1;
}

static int find_listen (uint32_t socket)
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
  connection[i].host = connection[i].rcv.link = connection[i].snd.link =
    connection[i].snd.size = connection[i].rcv.size = -1;
  connection[i].rcv.lsock = connection[i].rcv.rsock =
    connection[i].snd.lsock = connection[i].snd.rsock = 0;
  connection[i].flags = 0;
  connection[i].all_msgs = connection[i].all_bits = 0;
  connection[i].rrp_callback = NULL;
  connection[i].rfnm_callback = NULL;
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

#if 0
  {
    int i;
    for (i = 12; i < 12 + 2*words; i += 2)
      fprintf (stderr, " <<< %06o (%03o %03o)\n",
               (packet[i] << 8) | packet[i+1], packet[i], packet[i+1]);
  }
#endif

  if (type == IMP_REGULAR)
    hosts[destination].outstanding_rfnm++;

  imp_send_message (packet, words);
}

static void send_leader_error (int subtype)
{
  send_imp (0, IMP_LEADER_ERROR, 0, 0, 0, subtype, NULL, 2);
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

static int make_open (int host,
                      uint32_t rcv_lsock, uint32_t rcv_rsock,
                      uint32_t snd_lsock, uint32_t snd_rsock)
{
  int i = find_link (-1, -1);
  if (i == -1) {
    fprintf (stderr, "NCP: Table full.\n");
    return -1;
  }

  connection[i].host = host;
  connection[i].rcv.lsock = rcv_lsock;
  connection[i].rcv.rsock = rcv_rsock;
  connection[i].snd.lsock = snd_lsock;
  connection[i].snd.rsock = snd_rsock;
  connection[i].flags = 0;
  connection[i].rrp_time = time_tick - 1;
  connection[i].rfnm_time = time_tick - 1;

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
  send_ncp (destination, 8, 10, NCP_STR);
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
  send_ncp (destination, 8, 10, NCP_RTS);
}

// Allocate.
void ncp_all (uint8_t destination, uint8_t link, uint16_t msg_space, uint32_t bit_space)
{
  packet[22] = link;
  packet[23] = msg_space >> 8;
  packet[24] = msg_space;
  packet[25] = bit_space >> 24;
  packet[26] = bit_space >> 16;
  packet[27] = bit_space >> 8;
  packet[28] = bit_space;
  send_ncp (destination, 8, 8, NCP_ALL);
}

// Return.
void ncp_ret (uint8_t destination, uint8_t link, uint16_t msg_space, uint32_t bit_space)
{
  packet[22] = link;
  packet[23] = msg_space >> 16;
  packet[24] = msg_space;
  packet[25] = bit_space >> 24;
  packet[26] = bit_space >> 16;
  packet[27] = bit_space >> 8;
  packet[28] = bit_space;
  send_ncp (destination, 8, 8, NCP_RET);
}

// Give back.
void ncp_gvb (uint8_t destination, uint8_t link, uint8_t fm, uint8_t fb)
{
  packet[22] = link;
  packet[23] = fm;
  packet[24] = fb;
  send_ncp (destination, 8, 4, NCP_GVB);
}

// Interrupt by receiver.
void ncp_inr (uint8_t destination, uint8_t link)
{
  packet[22] = link;
  send_ncp (destination, 8, 2, NCP_INR);
}

// Interrupt by sender.
void ncp_ins (uint8_t destination, uint8_t link)
{
  packet[22] = link;
  send_ncp (destination, 8, 2, NCP_INS);
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
  send_ncp (destination, 8, 9, NCP_CLS);
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
  packet[22] = code;
  memcpy (packet + 23, data, length > 10 ? 10 : length);
  if (length < 10)
    memset (packet + 23 + length, 0, 10 - length);
  send_ncp (destination, 8, 12, NCP_ERR);
}

static int process_nop (uint8_t source, uint8_t *data)
{
  return 0;
}

static uint32_t sock (uint8_t *data)
{
  uint32_t x;
  x = data[0];
  x = (x << 8) | data[1];
  x = (x << 8) | data[2];
  x = (x << 8) | data[3];
  return x;
}

static void reply_open (uint8_t host, uint32_t socket, uint8_t connection)
{
  uint8_t reply[7];
  reply[0] = WIRE_OPEN+1;
  reply[1] = host;
  reply[2] = socket >> 24;
  reply[3] = socket >> 16;
  reply[4] = socket >> 8;
  reply[5] = socket;
  reply[6] = connection;
  if (sendto (fd, reply, sizeof reply, 0, (struct sockaddr *)&client, len) == -1)
    fprintf (stderr, "NCP: sendto %s error: %s.\n",
             client.sun_path, strerror (errno));
}

static void reply_listen (uint8_t host, uint32_t socket, uint8_t connection)
{
  uint8_t reply[7];
  reply[0] = WIRE_LISTEN+1;
  reply[1] = host;
  reply[2] = socket >> 24;
  reply[3] = socket >> 16;
  reply[4] = socket >> 8;
  reply[5] = socket;
  reply[6] = connection;
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

static void maybe_reply (int i)
{
  if ((connection[i].flags & CONN_GOT_BOTH) == CONN_GOT_BOTH) {
    fprintf (stderr, "NCP: Server got both RTS and STR from client.\n");
    connection[i].flags &= ~CONN_SERVER;
    reply_listen (connection[i].host, connection[i].listen, i);
  } else if ((connection[i].flags & CONN_GOT_ALL) == CONN_GOT_ALL) {
    fprintf (stderr, "NCP: Client got RTS, STR, and socket from server.\n");
    reply_open (connection[i].host, connection[i].listen, i);
  }
}

static int process_rts (uint8_t source, uint8_t *data)
{
  int i;
  uint32_t lsock, rsock;
  uint8_t link;

  rsock = sock (&data[0]);
  lsock = sock (&data[4]);
  link = data[8];

  fprintf (stderr, "NCP: Received RTS sockets %u:%u link %u from %03o.\n",
           rsock, lsock, link, source);

  if (link < LINK_MIN || link > LINK_MAX) {
    ncp_err (source, ERR_PARAM, data - 1, 10);
    return 9;
  }

  if (find_listen (lsock) != -1) {
    /* A server is listening to this socket, and a client has sent the
       RTS to initiate a new connection.  Reply with an STR for the
       initial part of ICP, which is to send the server data
       connection socket. */
    i = make_open (source, 0, 0, lsock, rsock);
    fprintf (stderr, "NCP: Listening to %u: new connection %d, link %u.\n",
             lsock, i, link);
    connection[i].flags |= CONN_SERVER;
    connection[i].snd.size = 32; //Send byte size for ICP.
    connection[i].snd.link = link;
    fprintf (stderr, "NCP: Confirm RTS, send STR sockets %u:%u size %u.\n",
             connection[i].snd.lsock,
             connection[i].snd.rsock,
             connection[i].snd.size);
    ncp_str (connection[i].host,
             connection[i].snd.lsock,
             connection[i].snd.rsock,
             connection[i].snd.size);
    connection[i].flags |= CONN_SENT_STR;
  } else if ((i = find_snd_sockets (source, lsock, rsock)) != -1) {
    /* There already exists a connection for this socket pair, which
       means an STR was sent previously.  */
    fprintf (stderr, "NCP: Confirmed STR, connection %d link %u.\n", i, link);
    connection[i].snd.link = link;
    connection[i].flags |= CONN_GOT_RTS;
    maybe_reply (i);
  } else if ((i = find_rcv_sockets (source, lsock-1, rsock+1)) != -1) {
    /* There already exists a connection for this socket pair, but
       only an STR was sent previously. */
    connection[i].snd.lsock = lsock;
    connection[i].snd.rsock = rsock;
    connection[i].snd.link = link;
    connection[i].snd.size = 8;
    connection[i].flags |= CONN_GOT_RTS;
    fprintf (stderr, "NCP: Confirm RTS, send STR sockets %u:%u size %u.\n",
             connection[i].snd.lsock,
             connection[i].snd.rsock,
             connection[i].snd.size);
    ncp_str (connection[i].host,
             connection[i].snd.lsock,
             connection[i].snd.rsock,
             connection[i].snd.size);
    connection[i].flags |= CONN_SENT_STR;
    maybe_reply (i);
  } else {
    /* There is no connection for this socket pair. */
    i = make_open (source, 0, 0, lsock, rsock);
    connection[i].snd.size = 8; //Send byte size.
    connection[i].snd.link = link;
    connection[i].flags |= CONN_GOT_RTS;
    fprintf (stderr, "NCP: New connection %d link %u.\n", i, link);
    fprintf (stderr, "NCP: Confirm RTS, send STR sockets %u:%u size %u.\n",
             connection[i].snd.lsock,
             connection[i].snd.rsock,
             connection[i].snd.size);
    ncp_str (connection[i].host,
             connection[i].snd.lsock,
             connection[i].snd.rsock,
             connection[i].snd.size);
    connection[i].flags |= CONN_SENT_STR;
  }

  return 9;
}

/* Incoming STR.

There are two cases to consider:

- This could be an STR from a server accepting the first client RTS.

- Otherwise, it must be an RTS from a client or server wanting to
  complete ICP.
*/
static int process_str (uint8_t source, uint8_t *data)
{
  int i;
  uint32_t lsock, rsock;
  uint8_t size;

  rsock = sock (&data[0]);
  lsock = sock (&data[4]);
  size = data[8];

  fprintf (stderr, "NCP: Received STR sockets %u:%u size %u from %03o.\n",
           rsock, lsock, size, source);

  if ((i = find_rcv_sockets (source, lsock, rsock)) != -1) {
    /* There already exists a connection for this socket pair, which
       means an RTS was sent previously.  */
    fprintf (stderr, "NCP: Confirmed RTS, connection %d.\n", i);
    connection[i].rcv.size = size;
    if (connection[i].flags & CONN_CLIENT) {
      ncp_all (source, connection[i].rcv.link, 1, 1000);
    } else {
      connection[i].flags |= CONN_GOT_STR;
      maybe_reply (i);
    }
  } else if ((i = find_snd_sockets (source, lsock+1, rsock-1)) != -1) {
    /* There already exists a connection for this socket pair, but
       only an RTS was sent previously. */
    connection[i].rcv.lsock = lsock;
    connection[i].rcv.rsock = rsock;
    connection[i].rcv.size = size;
    connection[i].rcv.link = 45;
    connection[i].flags |= CONN_GOT_STR;
    fprintf (stderr, "NCP: Confirm STR, send RTS sockets %u:%u link %u.\n",
             connection[i].rcv.lsock,
             connection[i].rcv.rsock,
             connection[i].rcv.link);
    ncp_rts (connection[i].host,
             connection[i].rcv.lsock,
             connection[i].rcv.rsock,
             connection[i].rcv.link);
    connection[i].flags |= CONN_SENT_RTS;
    maybe_reply (i);
  } else {
    /* There is no connection for this socket pair. */
    i = make_open (source, lsock, rsock, 0, 0);
    connection[i].rcv.size = size;
    connection[i].rcv.link = 43;
    connection[i].flags |= CONN_GOT_STR;
    fprintf (stderr, "NCP: New connection %d link %u.\n",
             i, connection[i].rcv.link);
    fprintf (stderr, "NCP: Confirm STR, send RTS sockets %u:%u link %u.\n",
             connection[i].rcv.lsock,
             connection[i].rcv.rsock,
             connection[i].rcv.link);
    ncp_rts (connection[i].host,
             connection[i].rcv.lsock,
             connection[i].rcv.rsock,
             connection[i].rcv.link);
    connection[i].flags |= CONN_SENT_RTS;
  }

  return 9;
}

/*
There are three cases to consider:

- This could be a CLS from a client or server closing the first ICP connection.

- It could be a CLS from a peer host wanting to close a normal connection.

- Or it would be a CLS in response to this host wanting to close a
  normal connection.
*/
static int process_cls (uint8_t source, uint8_t *data)
{
  int i;
  uint32_t lsock, rsock;
  rsock = sock (&data[0]);
  lsock = sock (&data[4]);
  i = find_sockets (source, lsock, rsock);
  if (i == -1) {
    fprintf (stderr, "NCP: Remote tried to close %u:%u which does not exist.\n",
             lsock, rsock);
    ncp_err (source, ERR_SOCKET, data - 1, 9);
    return 8;
  }

  fprintf (stderr, "NCP: Received CLS sockets %u:%u from %03o.\n",
           rsock, lsock, source);

  if (connection[i].rcv.lsock == lsock)
    connection[i].rcv.lsock = connection[i].rcv.rsock = 0;
  if (connection[i].snd.lsock == lsock)
    connection[i].snd.lsock = connection[i].snd.rsock = 0;

  if (connection[i].snd.size == -1) {
    // Remote confirmed closing.
    if (connection[i].rcv.lsock == 0 && connection[i].snd.lsock == 0) {
      fprintf (stderr, "NCP: Connection %u confirmed closed.\n", i);
      if ((connection[i].flags & (CONN_CLIENT | CONN_SERVER)) == 0)
        reply_close (i);
      destroy (i);
    }
  } else {
    // Remote closed connection.
    ncp_cls (connection[i].host, lsock, rsock);
    if (connection[i].rcv.lsock == 0 && connection[i].snd.lsock == 0) {
      fprintf (stderr, "NCP: Connection %u closed by remote.\n", i);
      destroy (i);
    }
  }

  return 8;
}

static void just_drop (int i)
{
  fprintf (stderr, "NCP: RFNM timeout, drop connection %d.\n", i);
  destroy (i);
}

static void cls_and_drop (int i)
{
  fprintf (stderr, "NCP: RFNM timeout, close connection %d.\n", i);
  ncp_cls (connection[i].host,
           connection[i].snd.lsock, connection[i].snd.rsock);
  ncp_cls (connection[i].host,
           connection[i].rcv.lsock, connection[i].rcv.rsock);
}

static void send_rts (int i)
{
  if (connection[i].flags & CONN_SENT_RTS)
    return;
  fprintf (stderr, "NCP: Send ICP RTS %u:%u link %d.\n",
           connection[i].rcv.lsock, connection[i].rcv.rsock,
           connection[i].rcv.link);
  ncp_rts (connection[i].host, connection[i].rcv.lsock,
           connection[i].rcv.rsock, connection[i].rcv.link);
  connection[i].flags |= CONN_SENT_RTS;
}

static void send_str_and_rts (int i)
{
  if ((connection[i].flags & CONN_SENT_STR) == 0) {
    fprintf (stderr, "NCP: Send ICP STR %u:%u byte size %d.\n",
             connection[i].snd.lsock, connection[i].snd.rsock,
             connection[i].snd.size);
    ncp_str (connection[i].host, connection[i].snd.lsock,
             connection[i].snd.rsock, connection[i].snd.size);
    connection[i].flags |= CONN_SENT_STR;
  }
  when_rfnm (i, send_rts, cls_and_drop);
  check_rfnm (connection[i].host);
}

static void send_cls_snd (int i)
{
  fprintf (stderr, "NCP: Close ICP %u:%u link %d.\n",
           connection[i].snd.lsock, connection[i].snd.rsock,
           connection[i].snd.link);
  ncp_cls (connection[i].host,
           connection[i].snd.lsock, connection[i].snd.rsock);
  connection[i].snd.size = connection[i].rcv.size = -1;
}

static void send_cls_rcv (int i)
{
  fprintf (stderr, "NCP: Close ICP %u:%u link %d.\n",
           connection[i].rcv.lsock, connection[i].rcv.rsock,
           connection[i].rcv.link);
  ncp_cls (connection[i].host,
           connection[i].rcv.lsock, connection[i].rcv.rsock);
  connection[i].snd.size = connection[i].rcv.size = -1;
}

static int process_all (uint8_t source, uint8_t *data)
{
  int i, j;
  uint8_t link = data[0];
  uint16_t msgs = data[1] << 8 | data[2];
  uint32_t bits = data[3] << 24 | data[4] << 16 | data[5] << 8 | data[6];

  fprintf (stderr, "NCP: Received ALL from %03o, link %u, msgs %u, bits %u.\n",
           source, link, msgs, bits);
  i = find_link (source, link);
  if (i == -1) {
    ncp_err (source, ERR_SOCKET, data - 1, 10);
  } else if (connection[i].flags & CONN_SERVER) {
    uint8_t tmp[9];
    uint32_t s = 0200;
    tmp[0] = 0;
    tmp[1] = 32;
    tmp[2] = 0;
    tmp[3] = 1;
    tmp[4] = 0;
    tmp[5] = (s >> 24) & 0xFF;
    tmp[6] = (s >> 16) & 0xFF;
    tmp[7] = (s >>  8) & 0xFF;
    tmp[8] = (s >>  0) & 0xFF;
    fprintf (stderr, "NCP: Send socket %u for ICP.\n", s);
    send_imp (0, IMP_REGULAR, connection[i].host, connection[i].snd.link,
              0, 0, &tmp, 7);
    j = make_open (connection[i].host,
                   s, connection[i].snd.rsock + 3,
                   s + 1, connection[i].snd.rsock + 2);
    if (j == -1) {
      // table full
      return 7;
    }
    connection[j].flags |= CONN_SERVER;
    connection[j].listen = listening[i].sock;
    fprintf (stderr, "NCP: New connection %d.\n", j);
    connection[j].snd.size = 8;
    connection[j].rcv.link = 44;
    when_rfnm (j, send_str_and_rts, cls_and_drop);
    when_rfnm (i, send_cls_snd, just_drop);
    check_rfnm (source);
  }
  connection[i].all_msgs += msgs;
  connection[i].all_bits += bits;
  return 7;
}

static int process_gvb (uint8_t source, uint8_t *data)
{
  int i;
  fprintf (stderr, "NCP: Received GBV from %03o, link %u.",
           source, data[0]);
  i = find_link (source, data[0]);
  if (i == -1)
    ncp_err (source, ERR_SOCKET, data - 1, 4);
  return 3;
}

static int process_ret (uint8_t source, uint8_t *data)
{
  int i;
  fprintf (stderr, "NCP: Received RET from %03o, link %u.",
           source, data[0]);
  i = find_link (source, data[0]);
  if (i == -1)
    ncp_err (source, ERR_SOCKET, data - 1, 8);
  return 7;
}

static int process_inr (uint8_t source, uint8_t *data)
{
  int i;
  fprintf (stderr, "NCP: Received INR from %03o, link %u.",
           source, data[0]);
  i = find_link (source, data[0]);
  if (i == -1)
    ncp_err (source, ERR_SOCKET, data - 1, 2);
  return 1;
}

static int process_ins (uint8_t source, uint8_t *data)
{
  int i;
  fprintf (stderr, "NCP: Received INS from %03o, link %u.",
           source, data[0]);
  i = find_link (source, data[0]);
  if (i == -1)
    ncp_err (source, ERR_SOCKET, data - 1, 2);
  return 1;
}

static int process_eco (uint8_t source, uint8_t *data)
{
  fprintf (stderr, "NCP: recieved ECO %03o from %03o, replying ERP %03o.\n",
           *data, source, *data);
  ncp_erp (source, *data);
  return 1;
}

static void reply_echo (uint8_t host, uint8_t data, uint8_t error)
{
  uint8_t reply[4];
  reply[0] = WIRE_ECHO+1;
  reply[1] = host;
  reply[2] = data;
  reply[3] = error;
  if (sendto (fd, reply, sizeof reply, 0,
              (struct sockaddr *)&hosts[host].echo.addr,
              hosts[host].echo.len) == -1)
    fprintf (stderr, "NCP: sendto %s error: %s.\n",
             hosts[host].echo.addr.sun_path, strerror (errno));
}

static int process_erp (uint8_t source, uint8_t *data)
{
  fprintf (stderr, "NCP: recieved ERP %03o from %03o.\n",
           *data, source);
  reply_echo (source, *data, 0x10);
  hosts[source].echo.len = 0;
  return 1;
}

static int process_err (uint8_t source, uint8_t *data)
{
  uint32_t rsock;
  int i;
  const char *meaning;
  switch (*data) {
  case ERR_UNDEFINED: meaning = "Undefined"; break;
  case ERR_OPCODE:    meaning = "Illegal opcode"; break;
  case ERR_SHORT:     meaning = "Short parameter space"; break;
  case ERR_PARAM:     meaning = "Bad parameters"; break;
  case ERR_SOCKET:    meaning = "Request on a non-existent socket"; break;
  case ERR_CONNECT:   meaning = "Socket (link) not connected"; break;
  default: meaning = "Unknown"; break;
  }
  fprintf (stderr, "NCP: recieved ERR code %03o from %03o: %s.\n",
           *data, source, meaning);
  fprintf (stderr, "NCP: error data:");
  for (i = 1; i < 11; i++)
    fprintf (stderr, " %03o", data[i]);
  fprintf (stderr, "\n");

  if ((data[0] == ERR_SOCKET || data[0] == ERR_CONNECT) &&
      (data[1] == NCP_RTS || data[1] == NCP_STR)) {
    rsock = sock (data + 6);
    i = find_sockets (source, sock (data + 2), rsock);
    if (i != -1) {
      if ((rsock & 1) == 0)
        rsock--;
      reply_open (source, rsock, 255);
      destroy (i);
    }
  }

  return 11;
}

static void reset (void)
{
  int i;
  for (i = 0; i < CONNECTIONS; i ++) {
    destroy (i);
    listening[i].sock = 0;
  }
  memset (hosts, 0, sizeof hosts);
}

static void reset_host (int host)
{
  int i;
  for (i = 0; i < CONNECTIONS; i++) {
    if (connection[i].host != host)
      continue;
    destroy (i);
  }
}

static int process_rst (uint8_t source, uint8_t *data)
{
  fprintf (stderr, "NCP: recieved RST from %03o.\n", source);
  hosts[source].flags |= HOST_ALIVE;

  if (hosts[source].echo.len > 0) {
    reply_echo (source, 0, 0x10);
    hosts[source].echo.len = 0;
  }

  reset_host(source);
  ncp_rrp (source);
  return 0;
}

static int process_rrp (uint8_t source, uint8_t *data)
{
  fprintf (stderr, "NCP: recieved RRP from %03o.\n", source);
  hosts[source].flags |= HOST_ALIVE;
  check_rrp (source);
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
  int i = 0, n;
  while (i < count) {
    uint8_t type = data[i++];
    if (type > NCP_MAX) {
      ncp_err (source, ERR_OPCODE, data - 1, 10);
      return;
    }
    n = ncp_messages[type] (source, &data[i]);
    if (i + n > count)
      ncp_err (source, ERR_SHORT, data - 1, count - i + 1);
    i += n;
  }
}

static void reply_read (uint8_t connection, uint8_t *data, int n)
{
  static uint8_t reply[1000];
  reply[0] = WIRE_READ+1;
  reply[1] = connection;
  memcpy (reply + 2, data, n);
  if (sendto (fd, reply, n + 2, 0, (struct sockaddr *)&client, len) == -1)
    fprintf (stderr, "NCP: sendto %s error: %s.\n",
             client.sun_path, strerror (errno));
}

static void process_regular (uint8_t *packet, int length)
{
  uint8_t source = packet[1];
  uint8_t link = packet[2];
  int i, j;

  uint8_t size = packet[5];
  uint16_t count = (packet[6] << 8) | packet[7];

  if (link == 0) {
    process_ncp (source, &packet[9], count);
  } else {
    fprintf (stderr, "NCP: process regular from %03o link %u.\n",
             source, link);
    i = find_link (source, link);
    if (i == -1) {
      fprintf (stderr, "NCP: Link not connected.\n");
      return;
    }
    fprintf (stderr, "NCP: Connection %u, byte size %u, byte count %u.\n",
             i, size, count);

    if (connection[i].rcv.size != size) {
      fprintf (stderr, "NCP: Wrong byte size, should be %d.\n",
               connection[i].rcv.size);
      return;
    }

    if (connection[i].flags & CONN_CLIENT) {
      uint32_t s = sock (&packet[9]);
      fprintf (stderr, "NCP: ICP link %u socket %u.\n", link, s);
      when_rfnm (i, send_cls_rcv, just_drop);
      connection[i].snd.rsock = s;

      j = find_rcv_sockets (source, connection[i].rcv.lsock+2, s+1);
      if (j == -1)
        j = find_snd_sockets (source, connection[i].rcv.lsock+3, s);
      if (j == -1) {
        j = make_open (source,
                       connection[i].rcv.lsock+2, s+1,
                       connection[i].rcv.lsock+3, s);
        connection[j].snd.size = 8;
        connection[j].rcv.link = 45;
        fprintf (stderr, "NCP: New connection %d.\n", j);
        when_rfnm (j, send_str_and_rts, cls_and_drop);
      }
      connection[j].listen = connection[i].rcv.rsock;
      connection[j].flags |= CONN_GOT_SOCKET;
      check_rfnm (connection[j].host);
      maybe_reply (j);

      return;
    }

    reply_read (i, packet + 9, count);
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
  uint8_t host = packet[1];
  fprintf (stderr, "NCP: Ready for next message to host %03o link %u.\n",
           host, packet[2]);
  hosts[host].outstanding_rfnm--;
  check_rfnm (host);
}

static void process_full (uint8_t *packet, int length)
{
  fprintf (stderr, "NCP: Link table full.\n");
}

static void process_host_dead (uint8_t *packet, int length)
{
  const char *reason;
  uint8_t host = packet[1];

  switch (packet[3] & 0x0F) {
  case 0: reason = "IMP cannot be reached"; break;
  case 1: reason = "is not up"; break;
  case 3: reason = "communication administratively prohibited"; break;
  default: reason = "dead, unknown reason"; break;
  }
  fprintf (stderr, "NCP: Host %03o %s.\n", host, reason);

  if (hosts[host].echo.len > 0) {
    reply_echo (host, 0, packet[3] & 0x0F);
    hosts[host].echo.len = 0;
  }

  hosts[host].flags &= ~HOST_ALIVE;
  reset_host(host);
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

#if 0
  {
    int i;
    for (i = 0; i < 2 * length; i+=2)
      fprintf (stderr, " >>> %06o (%03o %03o)\n",
               (packet[i] << 8) | packet[i+1], packet[i], packet[i+1]);
  }
#endif

  if (length < 2) {
    fprintf (stderr, "NCP: leader too short.\n");
    send_leader_error (1);
    return;
  }
  type = packet[0] & 0x0F;
  if (type <= IMP_RESET)
    imp_messages[type] (packet, length);
  else {
    fprintf (stderr, "NCP: leader type bad.\n");
    send_leader_error (2);
  }
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
  reset();

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
  uint8_t host = app[1];

  fprintf (stderr, "NCP: Application echo.\n");

  if (hosts[host].echo.len > 0) {
    reply_echo (host, 0, 0x20);
    return;
  }

  memcpy (&hosts[host].echo.addr, &client, len);
  hosts[host].echo.len = len;
  hosts[host].erp_time = time_tick + ERP_TIMEOUT;
  ncp_eco (host, app[2]);
}

static void app_open_rts (int i)
{
  // Send first ICP RTS.
  ncp_rts (connection[i].host, connection[i].rcv.lsock,
           connection[i].rcv.rsock, connection[i].rcv.link);
}

static void app_open_fail (int i)
{
  fprintf (stderr, "NCP: Timed out waiting for RRP.\n");
  reply_open (connection[i].host, connection[i].rcv.rsock, 255);
}

static void app_open (void)
{
  uint32_t socket;
  uint8_t host = app[1];
  int i;

  socket = app[2] << 24 | app[3] << 16 | app[4] << 8 | app[5];
  fprintf (stderr, "NCP: Application open socket %u on host %03o.\n",
           socket, host);

  // Initiate a connection.
  i = make_open (host, 1002, socket, 0, 0);
  connection[i].rcv.link = 42; //Receive link.
  connection[i].flags |= CONN_CLIENT;
  memcpy (&connection[i].client.addr, &client, len);
  connection[i].client.len = len;

  if ((hosts[host].flags & HOST_ALIVE) == 0) {
    // We haven't communicated with this host yet, send reset and wait.
    ncp_rst (host);
    when_rrp (i, app_open_rts, app_open_fail);
  } else {
    // Ok to send RTS directly.
    app_open_rts (i);
  }
}

static void app_listen (void)
{
  uint32_t socket;
  int i;

  socket = app[1] << 24 | app[2] << 16 | app[3] << 8 | app[4];
  fprintf (stderr, "NCP: Application listen to socket %u.\n", socket);
  if (find_listen (socket) != -1) {
    fprintf (stderr, "NCP: Alreay listening to %d.\n", socket);
    reply_listen (0, socket, 0);
    return;
  }
  i = find_listen (0);
  if (i == -1) {
    fprintf (stderr, "NCP: Table full.\n");
    reply_listen (0, socket, 0);
    return;
  }
  listening[i].sock = socket;
  memcpy (&connection[i].client.addr, &client, len);
  connection[i].client.len = len;
}

static void app_read (void)
{
  int i;
  i = app[1];
  fprintf (stderr, "NCP: Application read %u octets from connection %u.\n",
           app[2], i);
  ncp_all (connection[i].host, connection[i].rcv.link, 1, 8 * app[2]);
}

static void reply_write (uint8_t connection, uint16_t length)
{
  uint8_t reply[4];
  reply[0] = WIRE_WRITE+1;
  reply[1] = connection;
  reply[2] = length >> 8;
  reply[3] = length;
  if (sendto (fd, reply, sizeof reply, 0, (struct sockaddr *)&client, len) == -1)
    fprintf (stderr, "NCP: sendto %s error: %s.\n",
             client.sun_path, strerror (errno));
}

static void app_write (int n)
{
  int i = app[1];
  fprintf (stderr, "NCP: Application write, %u bytes to connection %u.\n",
           n, i);
  packet[16] = 0;
  packet[17] = connection[i].snd.size;
  packet[18] = n >> 8;
  packet[19] = n;
  packet[20] = 0;
  memcpy(packet + 21, app + 2, n);
  send_imp (0, IMP_REGULAR, connection[i].host, connection[i].snd.link, 0, 0,
            NULL, 2 + (n + 6) / 2);
  connection[i].all_msgs--;
  connection[i].all_bits -= 8 * n;
  reply_write (i, n);
}

static void app_interrupt (void)
{
  int i = app[1];
  fprintf (stderr, "NCP: Application interrupt, connection %u.\n", i);
  ncp_ins (connection[i].host, connection[i].snd.link);
}

static void app_close (void)
{
  int i = app[1];
  fprintf (stderr, "NCP: Application close, connection %u.\n", i);
  connection[i].snd.size = connection[i].rcv.size = -1;
  ncp_cls (connection[i].host, connection[i].rcv.lsock, connection[i].rcv.rsock);
  ncp_cls (connection[i].host, connection[i].snd.lsock, connection[i].snd.rsock);
}

static void application (void)
{
  ssize_t n;

  len = sizeof client;
  n = recvfrom (fd, app, sizeof app, 0, (struct sockaddr *)&client, &len);
  if (n == -1) {
    fprintf (stderr, "NCP: recvfrom error.\n");
    return;
  }

  fprintf (stderr, "NCP: Received application request %u from %s.\n",
           app[0], client.sun_path);

  if (!wire_check (app[0], n)) {
    fprintf (stderr, "NCP: bad application request.\n");
    return;
  }

  switch (app[0]) {
  case WIRE_ECHO:       app_echo (); break;
  case WIRE_OPEN:       app_open (); break;
  case WIRE_LISTEN:     app_listen (); break;
  case WIRE_READ:       app_read (); break;
  case WIRE_WRITE:      app_write (n - 2); break;
  case WIRE_INTERRUPT:  app_interrupt (); break;
  case WIRE_CLOSE:      app_close (); break;
  default:              fprintf (stderr, "NCP: bad application request.\n"); break;
  }
}

static void tick (void)
{
  void (*to) (int);
  int i;
  time_tick++;
  for (i = 0; i < CONNECTIONS; i++) {
    to = connection[i].rrp_timeout;
    if (to != NULL && connection[i].rrp_time == time_tick) {
      connection[i].rrp_callback = NULL;
      connection[i].rrp_timeout = NULL;
      connection[i].rrp_time = time_tick - 1;
      to (i);
    }
    to = connection[i].rfnm_timeout;
    if (to != NULL && connection[i].rfnm_time == time_tick) {
      connection[i].rfnm_callback = NULL;
      connection[i].rfnm_timeout = NULL;
      connection[i].rrp_time = time_tick - 1;
      to (i);
    }
  }
  for (i = 0; i < 256; i++) {
    if (hosts[i].echo.len == 0)
      continue;
    if (hosts[i].erp_time != time_tick)
      continue;
    reply_echo (i, 0, 0x20);
    hosts[i].echo.len = 0;
  }
}

static void cleanup (void)
{
  unlink (server.sun_path);
}

static void sigcleanup (int sig)
{
  cleanup ();
}

void ncp_init (void)
{
  char *path;

  fd = socket (AF_UNIX, SOCK_DGRAM, 0);
  memset (&server, 0, sizeof server);
  server.sun_family = AF_UNIX;
  path = getenv ("NCP");
  strncpy (server.sun_path, path, sizeof server.sun_path - 1);
  if (bind (fd, (struct sockaddr *)&server, sizeof server) == -1) {
    fprintf (stderr, "NCP: bind error: %s.\n", strerror (errno));
    exit (1);
  }
  signal (SIGINT, sigcleanup);
  signal (SIGQUIT, sigcleanup);
  signal (SIGTERM, sigcleanup);
  atexit (cleanup);
  time_tick = 0;
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
    struct timeval tv;
    FD_ZERO (&rfds);
    FD_SET (fd, &rfds);
    imp_fd_set (&rfds);
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    n = select (33, &rfds, NULL, NULL, &tv);
    if (n == -1)
      fprintf (stderr, "NCP: select error.\n");
    else if (n == 0) {
      tick ();
      tv.tv_sec = 1;
      tv.tv_usec = 0;
    } else {
      if (imp_fd_isset (&rfds)) {
        memset (packet, 0, sizeof packet);
        imp_receive_message (packet, &n);
        if (n > 0)
          process_imp (packet, n);
      }
      if (FD_ISSET (fd, &rfds)) {
        application ();
      }
    }
  }
}
