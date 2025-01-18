/* Definitions for protocol between libncp and ncp. */

#define WIRE_ECHO        1
#define WIRE_OPEN        3
#define WIRE_LISTEN      5
#define WIRE_READ        7
#define WIRE_WRITE       9
#define WIRE_INTERRUPT  11
#define WIRE_CLOSE      13

static int wire_check (int type, int size)
{
  switch (type) {
  case WIRE_ECHO:        return size == 3;
  case WIRE_ECHO+1:      return size == 4;
  case WIRE_OPEN:        return size == 7;
  case WIRE_OPEN+1:      return size == 9;
  case WIRE_LISTEN:      return size == 6;
  case WIRE_LISTEN+1:    return size == 8;
  case WIRE_READ:        return size == 3;
  case WIRE_READ+1:      return 1;
  case WIRE_WRITE:       return 1;
  case WIRE_WRITE+1:     return size == 4;
  case WIRE_INTERRUPT:   return size == 2;
  case WIRE_INTERRUPT+1: return size == 2;
  case WIRE_CLOSE:       return size == 2;
  case WIRE_CLOSE+1:     return size == 2;
  default:               return 0;
  }
}
