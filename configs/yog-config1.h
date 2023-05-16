#ifndef __YOG_CONF_H
#define __YOG_CONF_H

// need to be changed
// #define NEEDARBITER
static const char flow_filename[] = "/local/dpdk-stable-21.11.2/app/test-pmd/config/flow_info_gen_yog_homa.txt";
/* Configuration files to be placed in app/test-pmd/config/ */
/* The first line (server_id=0) is used for warm-up receiver */
static const char ethaddr_filename[] = "/local/dpdk-stable-21.11.2/app/test-pmd/config/eth_addr_info.txt";
static const char ipaddr_filename[] = "/local/dpdk-stable-21.11.2/app/test-pmd/config/ip_addr_info.txt";
/* The first few lines are used for warm-up flows */
int verbose_yog1 = 0;
int arbiter_server_id_yog1 = 1;


#endif
int this_server_id_yog1 = 8;
