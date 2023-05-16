#!/bin/bash
./dpdk-testpmd -l 0-3 -n 4 --proc-type=primary --socket-mem=8192 --file-prefix=.test1_config -a 03:00.1 -- --forward-mode=yog1
