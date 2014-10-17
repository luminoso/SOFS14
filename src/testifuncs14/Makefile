CC = gcc
CFLAGS = -g3 -gdwarf-2 -Wall -I "../debugging" -I "../rawIO14" -I "../sofs14"
LFLAGS = -L "../../lib"

all32:			testifuncs14_32

testifuncs14_32:	testifuncs14.o
			$(CC) $(LFLAGS) -o testifuncs14 $^ -lsofs14 -lsofs14bin_32 -lrawIO14bin_32 -lrawIO14 -ldebugging
			cp testifuncs14 ../../run
			rm -f $^ testifuncs14

all64:			testifuncs14_64

testifuncs14_64:	testifuncs14.o
			$(CC) $(LFLAGS) -o testifuncs14 $^ -lsofs14 -lsofs14bin_64 -lrawIO14bin_64 -lrawIO14 -ldebugging
			cp testifuncs14 ../../run
			rm -f $^ testifuncs14

clean:
			rm -f ../../run/testifuncs14
