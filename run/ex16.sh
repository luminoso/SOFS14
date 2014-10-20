#!/bin/bash

# This test vector deals mainly with operation attach a directory, detach a direntry and add a direntry.
# It defines a storage device with 98 blocks and formats it with 72 inodes.
# It starts by allocating ten inodes, associated to regular files, directories and symbolic links, and
# organize them in a hierarchical faction. Then it proceeds by moving one directory in the hierarchical
# tree.
# The showblock_sofs14 application should be used in the end to check metadata.

./createEmptyFile myDisk 98
./mkfs_sofs14 -n SOFS14 -i 72 -z myDisk
./testifuncs14 -b -l 300,700 -L testVector16.rst myDisk <testVector16.cmd
