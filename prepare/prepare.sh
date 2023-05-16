#!/bin/bash

set -e

export DEBIAN_FRONTEND=noninteractive

for user in `ls /users`; do
    sudo chsh -s `which bash` $user
done

sudo apt install python3-pip python3-pyelftools -y
pip3 install ninja meson

pushd /local
wget https://static.dpdk.org/rel/dpdk-21.11.2.tar.xz
tar xf dpdk-21.11.2.tar.xz

pushd dpdk-stable-21.11.2
mkdir -p app/test-pmd/config
meson setup build
popd
popd

chown -R yrpang:servelesslegoos- /local/dpdk-stable-21.11.2

pushd /local/repository/prepare/
cat add_to_bashrc.txt >> /users/yrpang/.bashrc
popd

echo "Finished init dpdk"