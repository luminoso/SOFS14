#!/bin/bash

fusermount -u mnt
rm val*rst

for i in {1..8}
do
 echo "A correr o ./val$i.sh"
 ./val$i.sh > val$i.rst
 ./showblock_sofs14 -s 0 myDisk > val$i-sb.rst
done

cat val*rst > val_allresults.rst 
fusermount -u mnt
