CFLAGS=-g -Wall

all: ncpd libncp.a

ncpd: ncp.o imp.o
	$(CC) -o $@ $^

libncp.a: libncp.o
	ar rcs $@ $^
	ranlib $@
