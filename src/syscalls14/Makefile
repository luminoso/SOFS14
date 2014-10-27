CC = gcc
CFLAGS = -g3 -gdwarf-2 -Wall -D_FILE_OFFSET_BITS=64 -I "../debugging" -I "../rawIO14" -I "../sofs14"
IFUNCS = soRead.o soReaddir.o soRename.o soTruncate.o soLink.o

all:			libsyscalls14

libsyscalls14:		$(IFUNCS)
			ar -r libsyscalls14.a $^
			cp libsyscalls14.a ../../lib
			rm -f $^ libsyscalls14.a

clean:
			rm -f ../../lib/libsyscalls14.a
			rm -f *.o libsyscalls14.a
