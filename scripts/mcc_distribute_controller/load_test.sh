#!/bin/bash

line=$1
id=$2
dest=$3

echo 10000000 > /proc/sys/fs/nr_open
ulimit -n 5000000

cd /home/mcctest/
tmp=${line%.*}
ip1=${tmp##*.}
ip2=${line##*.}
ifconfig dpdk0 192.168.$ip1.$ip2 netmask 255.255.0.0 up
ifconfig dpdk0 192.168.$ip1.$ip2 netmask 255.255.0.0 up

nohup ./worker -s 172.16.32.140 -l $line -n $id --network-stack mtcp --dest $dest --smp 2 --ips 200 -v 1 --log error > /dev/null 2>&1 

wall "Loader $id ..." 
