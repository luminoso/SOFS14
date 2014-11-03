CC = gcc
CFLAGS = -g3 -gdwarf-2  -Wall -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=26 -I "../debugging" -I "../rawIO14" -I "../sofs14" -I "../syscalls14"
LFLAGS = -L "../../lib" -L/lib

all32:			mount_sofs14_32

mount_sofs14_32:	mount_sofs14.o
			$(CC) $(LFLAGS) -o mount_sofs14 $^ -lsyscalls14 -lsyscalls14bin_32 -lsofs14 -lsofs14bin_32 -lrawIO14bin_32 \
			-lrawIO14 -ldebugging -lpthread -lfuse
			cp mount_sofs14 ../../run
			rm -f $^ mount_sofs14

all64:			mount_sofs14_64

mount_sofs14_64:	mount_sofs14.o
			$(CC) $(LFLAGS) -o mount_sofs14 $^ -lsyscalls14 -lsyscalls14bin_64 -lsofs14 -lsofs14bin_64 -lrawIO14bin_64 \
			-lrawIO14 -ldebugging -lpthread -lfuse
			cp mount_sofs14 ../../run
			rm -f $^ mount_sofs14

clean:
			rm -f ../../run/mount_sofs14
