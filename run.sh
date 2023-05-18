#!/bin/bash

set -e

SERVERS=("hp012" "hp039" "hp013" "hp024" "hp032" "hp014" "hp038" "hp037")
NUM_SERVERS=${#SERVERS[@]}

for i in {0..7}
do
    SERVER="${SERVERS[$i]}.utah.cloudlab.us"
    echo "$SERVER"

    scp ./expcode/testpmd/yog-server1.c yrpang@${SERVER}:/local/dpdk-stable-21.11.2/app/test-pmd/

    scp ./expcode/yog1.sh yrpang@${SERVER}:/local/dpdk-stable-21.11.2/build/app

    ssh $SERVER ninja -C /local/dpdk-stable-21.11.2/build 2>&1 > log/build_${i}.log &
done