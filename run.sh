#!/bin/bash

set -e

SERVERS=("hp039" "hp006" "hp038" "hp032")

cp ./configs/templates/eth_addr_info_template.txt ./configs/config/eth_addr_info.txt
for i in {0..3}
do
    SERVER="${SERVERS[$i]}.utah.cloudlab.us"
    ssh $SERVER "cat /sys/class/net/ens1f1np1/address" >> ./configs/config/eth_addr_info.txt
done


for i in {0..3}
do
    SERVER="${SERVERS[$i]}.utah.cloudlab.us"
    echo "$SERVER"

    cp ./configs/templates/yog-config-template.h ./configs/yog-config1.h
    echo "int this_server_id_yog1 = $[i+1];" >> ./configs/yog-config1.h

    if [ $i -eq 0 ]
    then
        echo "#define NEEDARBITER" >> ./configs/yog-config1.h
    fi


    scp -r ./configs/config/* yrpang@${SERVER}:/local/dpdk-stable-21.11.2/app/test-pmd/config

    scp ./expcode/testpmd/* yrpang@${SERVER}:/local/dpdk-stable-21.11.2/app/test-pmd/
    scp -r ./configs/yog-config1.h yrpang@${SERVER}:/local/dpdk-stable-21.11.2/app/test-pmd

    scp ./expcode/yog1.sh yrpang@${SERVER}:/local/dpdk-stable-21.11.2/build/app
    
    ssh $SERVER ninja -C /local/dpdk-stable-21.11.2/build
done