#!/bin/bash

# This test vector deals with the operations alloc inode and alloc / free data clusters.
# It defines a storage device with 100 blocks.
# It starts by allocating some inodes. Then, it allocates some data clusters and tests different error conditions. Finally, it frees the data
# clusters and also tests some error conditions, in particular that data cluster #0 can not be freed.
# The showblock_sofs14 application should be used in the end to check metadata.

./createEmptyFile myDisk 100
./mkfs_sofs14 -n SOFS14 -i 8 -z myDisk
./testifuncs14 -b -l 600,700 -L testVector3.rst myDisk <testVector3.cmd
