#!/bin/bash

# This test vector deals mainly with operation get a directory entry by path.
# It defines a storage device with 98 blocks and formats it with 72 inodes.
# It starts by allocating seven inodes, associated to regular files and directories, and organize them
# in a hierarchical faction. Then it proceeds by defining some symbolic links and trying to find
# different directory entries through the use of different paths containing symbolic links.
# The showblock_sofs14 application should be used in the end to check metadata.

./createEmptyFile myDisk 98
./mkfs_sofs14 -n SOFS14 -i 72 -z myDisk
./testifuncs14 -b -l 300,700 -L testVector15.rst myDisk <testVector15.cmd
