CFLAGS=-g -Wall

NCP=-L. -lncp

all: ncp ping

ncp: ncp.o imp.o

libncp.a: libncp.o
	ar rcs $@ $^
	ranlib $@

ping: ping.o libncp.a
	$(CC) -o $@ $< $(NCP)
