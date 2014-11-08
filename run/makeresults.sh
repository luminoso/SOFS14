#!/bin/bash

# delete all old results
rm *.rst

# run all ./exX.sh and save its superblocks results to files
for i in {1..16}
do
 echo "A correr ./ex$i.sh"
 ./ex$i.sh
 ./showblock_sofs14 -s 0 myDisk > ex$i-sb.rst
done

#merge all exX.sh results into one file
cat testVector*rst > ex_allresults.rst
cat ex*-sb.rst >> ex_allresults.rst

for i in {1..8}
do
 echo -e "A correr o ./val$i.sh"
 ./val$i.sh > val$i.rst
 ./showblock_sofs14 -s 0 myDisk > val$i-sb.rst
done

# merge all valX.sh results in one file
cat val*rst > val_allresults.rst 

# make sure if something failed that mnt dir is unmounted
fusermount -u mnt
