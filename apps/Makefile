CFLAGS=-g -Wall -I../src

NCP=-L../src -lncp
LIBNCP=../src/libncp.a
PREFIX=ncp-

all: $(PREFIX)ping $(PREFIX)finger $(PREFIX)finser $(PREFIX)telnet

$(PREFIX)ping: ping.o $(LIBNCP)
	$(CC) -o $@ $< $(NCP)

$(PREFIX)finger: finger.o $(LIBNCP)
	$(CC) -o $@ $< $(NCP)

$(PREFIX)finser: finser.o $(LIBNCP)
	$(CC) -o $@ $< $(NCP)

$(PREFIX)telnet: telnet.o tty.o $(LIBNCP)
	$(CC) -o $@ telnet.o tty.o $(NCP)

tty:: tty.h
