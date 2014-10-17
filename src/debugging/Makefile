CC = gcc
CFLAGS = -g3 -gdwarf-2 -Wall

all:			libdebugging

libdebugging:		sofs_probe.o
			ar -r libdebugging.a $^
			cp libdebugging.a ../../lib
			rm -f $^ libdebugging.a

clean:
			rm -f ../../lib/libdebugging.a
