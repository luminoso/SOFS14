CC = gcc
CFLAGS = -g3 -gdwarf-2  -Wall -I "../debugging" -I "../rawIO14"
IFUNCS1 = sofs_ifuncs_1/soAllocInode.o sofs_ifuncs_1/soFreeInode.o sofs_ifuncs_1/soAllocDataCluster.o \
	  sofs_ifuncs_1/soFreeDataCluster.o
IFUNCS2 = sofs_ifuncs_2/soWriteInode.o sofs_ifuncs_2/soCleanInode.o \
	  sofs_ifuncs_2/soAccessGranted.o sofs_ifuncs_2/soReadInode.o 
IFUNCS3 = sofs_ifuncs_3/soWriteFileCluster.o sofs_ifuncs_3/soHandleFileCluster.o \
	  sofs_ifuncs_3/soHandleFileClusters.o sofs_ifuncs_3/soReadFileCluster.o \
	  sofs_ifuncs_3/soCleanDataCluster.o
IFUNCS4 = sofs_ifuncs_4/soGetDirEntryByPath.o sofs_ifuncs_4/soGetDirEntryByName.o sofs_ifuncs_4/soAddAttDirEntry.o \
	  sofs_ifuncs_4/soRemDetachDirEntry.o sofs_ifuncs_4/soRenameDirEntry.o

all:			ifuncs1 ifuncs2 ifuncs3 ifuncs4 libsofs14

ifuncs1:
			make -C sofs_ifuncs_1 all

ifuncs2:
			make -C sofs_ifuncs_2 all

ifuncs3:
			make -C sofs_ifuncs_3 all

ifuncs4:
			make -C sofs_ifuncs_4 all

libsofs14:		sofs_blockviews.o sofs_basicoper.o $(IFUNCS1) $(IFUNCS2) $(IFUNCS3) $(IFUNCS4)
			ar -r libsofs14.a $^
			cp libsofs14.a ../../lib
			rm -f $^ libsofs14.a

clean:
			rm -f ../../lib/libsofs14.a
			rm -f *.o sofs_ifuncs_?/*.o libsofs14.a
