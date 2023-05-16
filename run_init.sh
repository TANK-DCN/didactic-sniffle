#!/bin/bash

set -e

SERVERS=("hp012" "hp026" "hp019" "hp034" "hp016" "hp033" "hp035" "hp027")

# mkdir -p ./configs/config
# cp ./configs/templates/eth_addr_info_template.txt ./configs/config/eth_addr_info.txt
# cp configs/templates/ip_addr_info.txt ./configs/config/
# cp configs/templates/flow_info_gen_yog_homa.txt ./configs/config/

# NUM_SERVERS=${#SERVERS[@]}
# for i in {0..${NUM_SERVERS}}
# do
#     SERVER="${SERVERS[$i]}.utah.cloudlab.us"
#     ssh $SERVER "cat /sys/class/net/ens1f1np1/address" >> ./configs/config/eth_addr_info.txt
# done


# for i in {0..${NUM_SERVERS}}
for i in {6..7}
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

    ssh $SERVER ninja -C /local/dpdk-stable-21.11.2/build 2>&1 > log/build_init_${i}.log &
done