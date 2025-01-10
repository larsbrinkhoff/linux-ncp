CFLAGS=-g -Wall

NCP=-L. -lncp

all: ncp ping finger finser telnet

ncp: ncp.o imp.o

libncp.a: libncp.o
	ar rcs $@ $^
	ranlib $@

ping: ping.o libncp.a
	$(CC) -o $@ $< $(NCP)

finger: finger.o libncp.a
	$(CC) -o $@ $< $(NCP)

finser: finser.o libncp.a
	$(CC) -o $@ $< $(NCP)

telnet: telnet.o tty.o libncp.a
	$(CC) -o $@ telnet.o tty.o $(NCP)

tty:: tty.h
