#!/bin/bash

# This test vector deals with the operations alloc / free inode.
# It defines a storage device with 19 blocks and formats it with an inode table of 16 inodes.
# It starts by allocating some inodes and tests an error condition. Then, it frees all the allocated inodes and tests different error conditions.
# The showblock_sofs14 application should be used in the end to check metadata.

./createEmptyFile myDisk 19
./mkfs_sofs14 -n SOFS14 -i 16 -z myDisk
./testifuncs14 -b -l 600,700 -L testVector1.rst myDisk <testVector1.cmd
