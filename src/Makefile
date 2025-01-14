CFLAGS=-g -Wall

all: ncpd

ncpd: ncp.o imp.o
	$(CC) -o $@ $^

libncp.a: libncp.o
	ar rcs $@ $^
	ranlib $@
