CFLAGS=-g -Wall -I../src

NCP=-L../src -lncp
LIBNCP=../src/libncp.a
PREFIX=ncp-

APPS=discard echo finger finser gateway ping telnet
PROGS=$(foreach i,$(APPS),$(PREFIX)$(i))

all: $(PROGS)

$(PREFIX)echo: echo.o $(LIBNCP)
	$(CC) -o $@ echo.o $(NCP)

$(PREFIX)discard: discard.o $(LIBNCP)
	$(CC) -o $@ discard.o $(NCP)

$(PREFIX)finger: finger.o $(LIBNCP)
	$(CC) -o $@ $< $(NCP)

$(PREFIX)finser: finser.o $(LIBNCP)
	$(CC) -o $@ $< $(NCP)

$(PREFIX)gateway: gateway.o inet.o $(LIBNCP)
	$(CC) -o $@ gateway.o inet.o $(NCP)

$(PREFIX)ping: ping.o $(LIBNCP)
	$(CC) -o $@ $< $(NCP)

$(PREFIX)telnet: telnet.o tty.o $(LIBNCP)
	$(CC) -o $@ telnet.o tty.o $(NCP)

inet.o:: inet.h

tty.o:: tty.h

.PHONY: clean
clean:
	rm -f $(PROGS) *.o
