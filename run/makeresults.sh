#!/bin/bash

fusermount -u mnt
rm val*rst

for i in {1..16}
do
 echo "A correr ./ex$i.sh"
 ./ex$i.sh
 ./showblock_sofs14 -s 0 myDisk > ex$i-sb.rst
done


for i in {1..8}
do
 echo -e "A correr o ./val$i.sh"
 ./val$i.sh > val$i.rst
 ./showblock_sofs14 -s 0 myDisk > val$i-sb.rst
done

cat val*rst > val_allresults.rst 
fusermount -u mnt
