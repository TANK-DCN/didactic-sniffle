/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2013 Tilera Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Tilera Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdlib.h>
#include <signal.h>

#include <sys/queue.h>
#include <sys/stat.h>

#include <rte_common.h>
#include <rte_byteorder.h>
#include <rte_log.h>
#include <rte_debug.h>
#include <rte_cycles.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_atomic.h>
#include <rte_branch_prediction.h>
#include <rte_memory.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_interrupts.h>
#include <rte_pci.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_string_fns.h>
#include <rte_timer.h>
#include <rte_malloc.h>
#include "yog-config1.h"


#include "testpmd.h"

// rte_spinlock_t send_lock_yog1;

bool SHOW_DEBUG_INFO_yog1 = false;
bool SAVE_DEBUG_INFO_yog1 = false;
bool DEBUG_LOCK_yog1 = false;
#define SHOW_SR_LOG 1
#define SAVE_SR_LOG 0

#define min(X,Y) ((X) < (Y) ? (X) : (Y))
#define max(X,Y) ((X) > (Y) ? (X) : (Y))

#define MAX_CONCURRENT_FLOW 9995 // 发送流个数
#define SERVERNUM 9             // 一共多少个IP，读取文件时需要
#define MAX_TIME 900            // in second  程序最多运行多久
int total_flow_num_yog1 = 9995;   // total flows among all servers
#define RTT_BYTES (25 * ((DEFAULT_PKT_SIZE) - (HDR_ONLY_SIZE))) // 这个需要测完自己填
#define RTT 0.003
#define TIME_OUT_TIME 3 * RTT
#define MASK (((1 << (SERVERNUM - 1)) - 1) - (1 << (this_server_id_yog1 - 1)))

#define log_level          1   //1  packet level    2 flow level
#define show_flow          1
#define show_host          1
#include <asm/types.h>
/** Create IPv4 address */
#define IPv4(a, b, c, d) ((__u32)(((a) & 0xff) << 24) | \
		(((b) & 0xff) << 16) | \
		(((c) & 0xff) << 8)  | \
		((d) & 0xff))

#define unsure              0x01
#define self_src            0x02
#define self_dst            0x03
#define order_src           0x04
#define order_dst           0x05
//！！！！！！！！！！！所有流量ID从1开始

#define TIMER_LCORE         2 
#define TIMER_INTERVAL_MS   0.000005//定时器触发间隔，这里是0.1秒

#define PT_INF_yog1O            0X20
#define PT_GRANT                0X21
#define PT_DATA                 0X22
#define PT_ACK                  0X23
#define PT_CHANGE_RANK_1        0X24
#define PT_CHANGE_RANK_2        0X25
#define PT_REFUSE               0X26
#define PT_WAIT                 0X27
#define PT_ORDER                0X28
#define PT_FLOW_FINISH          0X29
#define PT_SYNC                 0x10
#define ARBITER_PT_INF_yog1O    0X60
#define ARBITER_PT_GRANT        0X61
#define ARBITER_PT_REFUSE       0X66
#define ARBITER_PT_FLOW_FINISH  0X69

#define PT_INF_yog1O_ACK            0X30
#define PT_GRANT_ACK                0X31
#define PT_CHANGE_RANK_1_ACK        0X34
#define PT_CHANGE_RANK_2_ACK        0X35
#define PT_FLOW_FINISH_ACK          0X39
struct flow_info {//记录一条流的全部信息
    uint32_t dst_ip;
    uint32_t src_ip;
    uint16_t dst_port;
    uint16_t src_port;
    double   start_time;//流开始时间，从文件中进行读取
    uint32_t flow_size; /* flow total size */
    uint8_t  rank_in_sender;//流在发送端排名，初始化为-1
    uint8_t  flow_finished;
    int remain_size; /* 流当前剩余字节数*/ 

    //发送端需要的信息
    double   sender_finish_time;//发送端流结束时间
    uint32_t sender_unack_size;//发送还未接收到ACK的数据量
    uint32_t sender_acklen;//已经确认的数据长度
    uint32_t sender_can_send_size; /* 流当前可以发送的字节数*/ 
    uint32_t sender_wait_count;
    


    //接收端需要的信息
    uint32_t receiver_recvlen;//接收端接收到的数据长度,记得初始化
    double   receiver_finish_time;//接收端流结束时间
    uint32_t unexcept_pkt_count;


    bool is_pre_grant;
    
};

static struct rte_ether_addr eth_addr_array[SERVERNUM];
static uint32_t ip_addr_array[SERVERNUM];

//别忘了改回来ip mac地址啥的，封装起来吧


/* Output level:
    verbose_yog1 = 0; stage level; guarantees best performance
    verbose_yog1 = 1; flow level;
    verbose_yog1 = 2; packet level;
    verbose_yog1 = 3; all.           
*/
#define DEFAULT_PKT_SIZE 1500
#define L2_LEN sizeof(struct rte_ether_hdr)
#define L3_LEN sizeof(struct rte_ipv4_hdr)
#define L4_LEN sizeof(struct rte_tcp_hdr)
#define HDR_ONLY_SIZE (L2_LEN + L3_LEN + L4_LEN) 

/* Define ip header info */
#define IP_DEFTTL  64   /* from RFC 1340. */
#define IP_VERSION 0x40
#define IP_HDRLEN  0x05 /* default IP header length == five 32-bits words. */
#define IP_VHL_DEF (IP_VERSION | IP_HDRLEN)





/* Redefine TCP header fields for Homa */
#define PKT_TYPE_8BITS tcp_flags
// #define HANDLING_FLOW_ID_16BITS fragment_offset
#define HOST_CTL_TYPE_8BIT data_off
#define FLOW_REMAIN_SIZE sent_seq
#define FLOW_ID_16BITS rx_win
#define HANDLING_FLOW_ID_16BITS packet_id
// Homa grant request header ONLY
#define FLOW_SIZE_LOW_16BITS tcp_urp
#define FLOW_SIZE_HIGH_16BITS cksum
// Homa grant header ONLY
#define SEQ_GRANTED_LOW_16BITS tcp_urp
#define SEQ_GRANTED_HIGH_16BITS cksum
// Homa data header ONLY
#define DATA_LEN_16BITS tcp_urp
// Homa resend request header ONLY
#define DATA_RESEND_16BITS tcp_urp


#define MAX_GRANT_TRANSMIT_ONE_TIME 32
#define MAX_REQUEST_RETRANSMIT_ONE_TIME 16
#define TIMEOUT 0.01
#define GRANT_INTERVAL 0.000005
#define BURST_THRESHOLD 1



double start_cycle, elapsed_cycle;
double flowgen_start_time;
double warm_up_time_yog1 = 20.0; // in sec
double sync_start_time_yog1 = 3.0; // within warm_up_time_yog1
int    sync_done_yog1 = 0;
uint32_t sync_count;
double hz;
struct fwd_stream *global_fs;


struct flow_info *sender_flows;
struct flow_info *receiver_flows;
// struct flow_info *arbiter_flows;

struct rte_mbuf *sender_pkts_burst[MAX_PKT_BURST];
struct rte_mbuf *receiver_pkts_burst[MAX_PKT_BURST];
FILE* fp;
FILE* fct_fp;
char filename[256];

struct rte_mempool *send_pool_yog1 = NULL;

/* 构建有序set，利用链表实现
 */

typedef struct SetNode {
    int flow_id;
    struct SetNode* next;
} SetNode;

typedef struct Set {
    SetNode* head;
} Set;

//发送端流信息收集表
Set* sender_largeflow_arr;
Set* sender_smallflow_arr;


//接收端流信息收集表
Set* receiver_actflow_arr;
Set* receiver_firmflow_arr;
Set* receiver_smallflow_arr;

/* Sender task states */
int sender_total_flow_num_yog1_1            = 0;
int sender_grant_request_sent_flow_num_yog1 = 0;
int sender_finished_flow_num_yog1           = 0;
int sender_next_unstart_flow_id_yog1        = 0;
int sender_current_burst_size_yog1          = 0;
int sender_active_flow_num_yog1             = 0;
int sender_smallest_flow_id_yog1            = 0;
int sender_lager_flow_num_yog1              = 0;  
int sender_ctl_type_yog1                    = unsure;
int sending_flow_id_yog1                    = 0;
// int sender_smallest_flow_id_yog1            = 0;
int sender_small_flow_num_yog1              =  0;

/* Receiver task states */
int receiver_total_flow_num_yog1_1      = 0;
int receiver_active_flow_num_yog1     = 0; 
int max_receiver_active_flow_num_yog1_1 = 0;
int receiver_finished_flow_num_yog1   = 0;
int receiver_current_burst_size_yog1  = 0;
int receiver_ctl_type_yog1            = unsure;
int receiving_flow_id_yog1            = 0;
int receiver_firm_flow_num_yog1       = 0;

#ifdef NEEDARBITER
static Set *host_send_flows[SERVERNUM];       // 发送端所有的流
static int host_sending_flow_id_yog1[SERVERNUM];   // 发送端正在发送流
static int host_receiving_flow_id_yog1[SERVERNUM]; // 接收端正在接收流
static int host_sender_ctl_type_yog1[SERVERNUM];
static int host_receiver_ctl_type_yog1[SERVERNUM];
static Set *arbiter_flows_set;
#endif

static void
LOG_DEBUG(uint32_t log_le, const char *file, const int line, uint32_t flow_id, const char *str);

struct pktNode
{
    int flow_id;     
    int end_pos;
    int pkt_size;   
    double send_time;
    //add new type
    int pkt_type;
    struct pktNode* prev;  //前一个指针域
    struct pktNode* next;  //下一个指针域
    int ifvalid;
};

typedef struct
{
    struct pktNode* head; //头部指针
    struct pktNode* rear; //尾部指针
    int count;             //统计数量
} MyLinkedList;

MyLinkedList* Unackpkts_list;
MyLinkedList* ctl_pkt_list;

static rte_atomic64_t send_timer_signal;
static void send_timer_callback(__attribute__((unused)) struct rte_timer *t,
                            __attribute__((unused)) void *arg);




static void construct_data(uint32_t flow_id);
static void timeout_timer_callback(struct rte_timer *tim, void *arg);//一个数据包超时之后的操作

/* 下面代码用于实现端侧数据包的超时重传，利用timer实现 */
typedef struct timeout_arg{//用于timer传递参数的结构体
    int flow_id;
    uint32_t data_sended;
} timeout_arg;

static void timeout_timer_callback(struct rte_timer *tim, void *arg)
{
     if(SHOW_DEBUG_INFO_yog1) printf("enter data time out\n");
    //  fflush(stdout);
    // timeout_arg* to_arg = (timeout_arg*)arg;//读取参数
    // if(sender_flows[to_arg->flow_id].sender_acklen>=to_arg->data_sended){
    //     return;//如果没有超时
    // }
    // //如果超时了
    // sender_flows[to_arg->flow_id].sender_can_send_size+=(DEFAULT_PKT_SIZE-HDR_ONLY_SIZE);
    // sender_flows[to_arg->flow_id].sender_unack_size-=(DEFAULT_PKT_SIZE-HDR_ONLY_SIZE);
    // if(SHOW_DEBUG_INFO_yog1) printf("enter data time out flowid = %d data_sended = %d time = %lf\n",to_arg->flow_id,to_arg->data_sended,rte_rdtsc() / (double)hz);
}

static void statr_timeout_timer(int flow_id,uint32_t data_sended);
//开启一个新的超时重传函数
static void statr_timeout_timer(int flow_id,uint32_t data_sended){
    timeout_arg* new_arg = malloc(sizeof(timeout_arg));//新建参数
    new_arg->data_sended = data_sended;
    new_arg->flow_id = flow_id;
    struct rte_timer timer2;
    uint64_t hz = rte_get_timer_hz();
    rte_timer_init(&timer2);//初始化并开始

    // rte_timer_reset(&timer,  hz * TIME_OUT_TIME, SINGLE, TIMER_LCORE,timeout_timer_callback , (void*)new_arg);
    // rte_timer_reset(&timer2,  hz, SINGLE,0,timeout_timer_callback , (void*)new_arg);
    rte_timer_reset(&timer2,  hz, SINGLE,0,send_timer_callback ,NULL);
}

static int getset_cansend_flowid(Set* set);
//找到一个可以开启的flowid，用于找小流，找can send data大于0的
static int getset_cansend_flowid(Set* set){
    int ret_id;
    SetNode* curr_node;
    if(set->head==NULL)
        return -1;//返回不存在
    curr_node = set->head;
    while(curr_node!=NULL && sender_flows[curr_node->flow_id].sender_can_send_size<=0){
        curr_node = curr_node->next;
    }
    if(curr_node==NULL)
        return -1;
    return curr_node->flow_id;
}

static void
construct_ctl_pkt(int pkt_type,int flow_id,int where);//构建数据包并加入队列
//where = 1 正常到对侧端  where=2  发送到集中控制器


static void
send_data(void);

static inline int
get_src_server_id(uint32_t flow_id, struct flow_info *flows);

static inline int
get_dst_server_id(uint32_t flow_id, struct flow_info *flows);

static void find_timeout_pkt(void);
static void find_timeout_ctl_pkt(void);

static int removeElement(Set* set, int flow_id);

static void
send_data(void){
    int small_flow_id ;
    find_timeout_pkt();
    find_timeout_ctl_pkt();
    //如果端侧存在小流且有可以发送的小流
    // if(SHOW_DEBUG_INFO_yog1) printf("small flow num : %d",sender_small_flow_num_yog1);
    if(sender_small_flow_num_yog1>0 && 
    (small_flow_id = getset_cansend_flowid(sender_smallflow_arr))>0){

        // print debug info
        SetNode *temp = sender_smallflow_arr->head;
        if(SHOW_DEBUG_INFO_yog1) printf("send small flow = ");
        while (temp != NULL)
        {
            if(SHOW_DEBUG_INFO_yog1) printf(" id:  %d  can_Send: %d ", temp->flow_id, sender_flows[temp->flow_id].sender_can_send_size);
            temp = temp->next;
        }
        if(SHOW_DEBUG_INFO_yog1) printf("\n");

        if(small_flow_id<=0 || get_src_server_id(small_flow_id,sender_flows)!=this_server_id_yog1)
        {
            if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"small flow error with error src\n");
            removeElement(sender_smallflow_arr,small_flow_id);
            return;
        }


        construct_data(small_flow_id);

        // // statr_timeout_timer(small_flow_id,sender_flows[small_flow_id].sender_unack_size);
        // if(sending_flow_id_yog1 > 0 && sender_flows[sending_flow_id_yog1].sender_wait_count>0)
        //     sender_flows[sending_flow_id_yog1].sender_wait_count--;
        return;
    }
    // 如果没有小流
    // 如果没有可以sending flow
    if (sending_flow_id_yog1 <= 0)
        return;
    // 如果有，但是有wait计数
    // if (sender_flows[sending_flow_id_yog1].sender_wait_count > 0)
    // {
    //     sender_flows[sending_flow_id_yog1].sender_wait_count--;
    //     return;
    // }
    if (sender_flows[sending_flow_id_yog1].sender_can_send_size > 0)
        construct_data(sending_flow_id_yog1);
    // statr_timeout_timer(sending_flow_id_yog1,sender_flows[sending_flow_id_yog1].sender_unack_size);
}

static MyLinkedList* myLinkedListCreate(void);

static void myLinkedListAddAtTail(uint32_t flow_id,int end_pos ,int pkt_size, double send_time );//根据flow_id 和 时间创建节点并加入链表

static void myLinkedListDeleteHead(void);//删除链表的头节点

static void myLinkedListAddAtTail_ctl(uint32_t flow_id,int pkt_type , double send_time );//根据flow_id 和 时间创建节点并加入链表

static void myLinkedListDeleteHead_ctl(void);//删除链表的头节点

static
int myLinkedListDeletebyInfo_ctl(int pkt_type,uint32_t flow_id);

static void 
get_log_fp(void);

static void 
get_log_fp(void){
    time_t rawtime;
	struct tm * curTime;
	time ( &rawtime );
	curTime = localtime ( &rawtime );
	
	sprintf(filename,"%04d-%02d-%02d-%02d-%02d-%02d-%02d.txt",curTime->tm_year+1900,
	curTime->tm_mon+1,curTime->tm_mday,curTime->tm_hour,curTime->tm_min,curTime->tm_sec, this_server_id_yog1);
	fp = fopen(filename,"a");

    char fct_file[50];
    sprintf(fct_file, "fct_%d.txt", this_server_id_yog1);
    fct_fp = fopen(fct_file, "w");
}

// static void 
// write_log(uint32_t flow_id,uint8_t type);

// static void 
// write_log(uint32_t flow_id,uint8_t type){
//     if(type==1)//sender
//     {
//         if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"flow id = %d  flow_size:%d remain_size:%d  send_unack_size %d start_time%lf first_grant_access_time:%lf  send_acklen:%d unack_finish:%d",flow_id,
//                     sender_flows[flow_id].flow_size,sender_flows[flow_id].remain_size,sender_flows[flow_id].send_unack_size,sender_flows[flow_id].start_time,
//                     sender_flows[flow_id].first_grant_access_time-flowgen_start_time-warm_up_time_yog1,sender_flows[flow_id].send_acklen,sender_flows[flow_id].Unack_finish);
//     }
//     // else{
//     //     if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"scr_ip: %d  dst_ip:%d pro_type:%d flow_state:%d data_recv_next:%d recv_exceptseq:%d recv_recvlen:%d",
//     //                 receiver_flows[flow_id].src_ip,receiver_flows[flow_id].dst_ip,receiver_flows[flow_id].pro_type,receiver_flows[flow_id].flow_state,receiver_flows[flow_id].data_recv_next,receiver_flows[flow_id].recv_exceptseq,receiver_flows[flow_id].recv_recvlen);

//     // }
//        if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"\n");
// }

/* 
alt+shirt+a
// 构建有序set，利用链表实现
//  */

// typedef struct SetNode {
//     int flow_id;
//     struct SetNode* next;
// } SetNode;

// typedef struct Set {
//     SetNode* head;
// } Set;
//获取有序集合中的最小流flow_id
static int getset_smallest_flowid(Set* set);
static int getset_smallest_flowid(Set* set){
    if(set->head==NULL)
        return -1;//返回不存在
    return set->head->flow_id;
}
static Set* createSet(void);
// 创建一个空集合
static Set* createSet(void) {
    Set* set = malloc(sizeof(Set));
    set->head = NULL;
    return set;
}



static int addSet(Set* set, int flow_id,struct flow_info *info_arr);
// 向集合中添加元素,-1不成功 1成功
static int addSet(Set* set, int flow_id,struct flow_info *info_arr) {
    assert(set!=NULL);
    SetNode* new_node = malloc(sizeof(SetNode));
    new_node->flow_id = flow_id;
    new_node->next = NULL;

    // 如果集合为空，则直接将新节点作为头节点
    if (set->head == NULL) {
        set->head = new_node;
        return 1;
    }

    SetNode* current_node = set->head;
    SetNode* previous_node = NULL;

    // 找到第一个大于current节点大于当前节点的值的位置，在current与pre节点插入新的节点，当新节点已经存在于链表时，会访问当原本值，这样实现的复杂度为On，加快代码运行速度
    while (current_node != NULL &&  info_arr[current_node->flow_id].remain_size <= info_arr[flow_id].remain_size  && current_node->flow_id!=flow_id) {
        previous_node = current_node;
        current_node = current_node->next;
    }

    // 如果该元素已经存在，则直接返回
    if (current_node != NULL && current_node->flow_id == flow_id) {
        free(new_node);
        return -1;
    }
    // 插入新节点
    if (previous_node == NULL) {
        // 新节点应该成为头节点
        new_node->next = set->head;
        set->head = new_node;
    } else {
        // 新节点应该插入到 previous_node 和 current_node 之间
        previous_node->next = new_node;
        new_node->next = current_node;
    }
    return 1;
}

// 从集合中删除元素  1成功删除  -1没成功，不存在
static int removeElement(Set* set, int flow_id) {
    SetNode* current_node = set->head;
    SetNode* previous_node = NULL;

    while (current_node != NULL && current_node->flow_id!=flow_id) {
        previous_node = current_node;
        current_node = current_node->next;
    }

    if (current_node == NULL || current_node->flow_id != flow_id) {
        return -1;
    }
    if (previous_node == NULL) {
        set->head = current_node->next;
    } else {
        previous_node->next = current_node->next;
    }

    free(current_node);
    return 1;
}
static int contains(Set* set, int flow_id) ;
// 判断元素是否在集合中
static int contains(Set* set, int flow_id) {
    SetNode* current_node = set->head;

    while (current_node != NULL && current_node->flow_id!=flow_id) {
        current_node = current_node->next;
    }

    return current_node != NULL && current_node->flow_id == flow_id;
}

static void updateSet(Set* set, int flow_id,struct flow_info *info_arr);
//更新节点的位置，保证链表始终有序
static void updateSet(Set* set, int flow_id,struct flow_info *info_arr){
    int ret;
    ret = removeElement(set,flow_id);//移除节点
    if(ret==-1){
        RTE_LOG(WARNING, ACL, "update remove null node");
    }
    ret = addSet(set,flow_id,info_arr);//重新加入
    if(ret==-1)
    {
        RTE_LOG(WARNING, ACL, "add exit node");
    }
    return;
}

static void printSet(Set* set);
// 打印集合中的所有元素
static void printSet(Set* set) {
    SetNode* current_node = set->head;
    while (current_node != NULL) {
        if(SHOW_DEBUG_INFO_yog1) printf("%d ", current_node->flow_id);
        current_node = current_node->next;
    }
    if(SHOW_DEBUG_INFO_yog1) printf("\n");
}

//时钟回调函数，每次触发后进入该函数
static void send_timer_callback(__attribute__((unused)) struct rte_timer *t,
                            __attribute__((unused)) void *arg)
{
    // if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"当前时间： %lf",rte_rdtsc() / (double)hz - start_cycle);
    // if(SHOW_DEBUG_INFO_yog1) printf("当前时间： %lf\n",(rte_rdtsc()- start_cycle) / (double)hz );
    send_data();
    // construct_data(2418);
    // sending_flow_id_yog1 = 211;
    // receiving_flow_id_yog1 = 14;
    // construct_ctl_pkt(PT_GRANT, 857, 1);
}
static int send_timer_loop(__attribute__((unused)) void *arg);
//轮询函数，在指定线程安全值大于0时一直轮询时钟，利用单独线程开启该函数
static int send_timer_loop(__attribute__((unused)) void *arg)
{
    // if(SHOW_DEBUG_INFO_yog1) printf("timer lcore = %d\n",rte_lcore_id());
    double prev_tsc, cur_tsc;
    uint64_t hz = rte_get_timer_hz();
    
    struct rte_timer timer;
    rte_timer_init(&timer);//初始化timer
    rte_timer_reset(&timer,  hz*TIMER_INTERVAL_MS, PERIODICAL, TIMER_LCORE, send_timer_callback, NULL);//设置timer参数并开启
    // uint64_t next_timer_tick = rte_rdtsc() +  hz * TIMER_INTERVAL_MS / 1000;

    while (rte_atomic64_read(&send_timer_signal)>0) {

        rte_timer_manage();

    }
    return 0;
}
static void send_timer_end(void);
//通过将线程安全的值置为0将特定线程的函数结束
static void send_timer_end(void){
    while(rte_atomic64_read(&send_timer_signal)>0){
        rte_atomic64_sub(&send_timer_signal,1);
    }
}

static void
start_senddata_timer(void);
/* 开启时钟并将轮询函数放置到指定线程上*/

static void
start_senddata_timer(void){
    rte_atomic64_init(&send_timer_signal);//初始化发送数据的信号量
    rte_atomic64_add(&send_timer_signal,1);
    rte_timer_subsystem_init();//初始化timer 系统
    // RTE_LCORE_FOREACH_SLAVE(TIMER_LCORE){
	// 	rte_eal_remote_launch(send_timer_loop, NULL, TIMER_LCORE);
	// }
    int ret = rte_eal_remote_launch(send_timer_loop, NULL, TIMER_LCORE);//分配单独线程
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Cannot launch timer loop on lcore %d\n", TIMER_LCORE);
    return;
}


/* 
如何测试上述函数：
    由于该功能利用单独线程实现，所以在主函数中只能触发一次，所以就像init函数一样，在死循环之前尝试开启
    然后由于该线程需要结束，所以在主函数到达指定时间结束之前需要调用timer_end函数结束该进程

需要测试点：
    1 是否能编译通过
    2 是否可以通过控制timer_start值让进程开始/结束
    3 timer是否按照规定时间重复运行
 */


/* Declaration of functions */
static void
main_flowgen(struct fwd_stream *fs);

static void
start_new_flow(void);//已实现

static int
add_sender_largeflow_arr(int flow_id);    // 李宇龙
/* 实现功能：
    将给定流ID加入大流队列（Large_flow_arr（未定义））中
    该大流队列由最小堆实现，保证堆顶元素指向流sender_remain_size最小
    判断该流是否为端侧最小流，确定返回值
        这里判断该流是否为最小流有一种特殊情况，即当前端侧有流量正在发送，所以即使该流可以发送，那么也需要在RTT时间后才能被调度，所以该流被判定为最小流时需要比正在发送流小RTTBYTE
        在该流顶替其他流成为最小流时，原本最小流需要发送change_rank_2的ctl_pkt
    变量名：
        sender_smallest_flow_id_yog1;
        sender_lager_flow_num_yog1;
 */

static void
send_ctl_pkt(int pkt_type,int flow_id,int arbiter);
//控制包类型   flow_id     是否发送给集中控制器 1发送  0不发送
/* 实现功能：
    对给定流ID发送给定控制数据包，
    控制数据包中需要额外加入端侧的正在传输流ID以及端侧传输状态
    要根据flowid以及端侧ID确定传输的是发送端状态还是接收端状态
    要根据flowid以及端侧ID确定传输的是发送流还是接收流
    变量名：
    sender_ctl_type_yog1 / receiver_ctl_type_yog1 已经定义好  有宏值
    sending_flow_id_yog1  receiving_flow_id_yog1
 */

static int
add_sender_largeflow_arr(int flow_id){
    int ret = addSet(sender_largeflow_arr,flow_id,sender_flows);//加入大流有序列表
    if(ret==1)  sender_active_flow_num_yog1++;
    else return -1;
    if(getset_smallest_flowid(sender_largeflow_arr)==flow_id){//如果在该列表中该流最小
        if(sending_flow_id_yog1 > 0){//如果存在正在发送流，需要比正在发送流小一个RTTBYTES才可以被称为发送端最小流
            if(sender_flows[sending_flow_id_yog1].remain_size>sender_flows[flow_id].remain_size+RTT_BYTES){
                send_ctl_pkt(PT_CHANGE_RANK_2,sender_smallest_flow_id_yog1,1);
                sender_smallest_flow_id_yog1 = flow_id;
                return 1;
            }
        }
        else{//如果不存在正在发送流，那么就是发送端最小流
                
                sender_smallest_flow_id_yog1 = flow_id;
                // send_ctl_pkt(PT_CHANGE_RANK_1,sender_smallest_flow_id_yog1,1);
                return 1;
        }
    }
    return 2;
}

static void
add_sender_smallflow_arr(int flow_id);   // 李宇龙
/* 实现功能：
    将给定流ID加入小流队列（Small_flow_arr（未定义））中
    该小流队列由最小堆实现，保证堆顶元素指向流sender_remain_size最小
    变量名：
        sender_small_flow_num_yog1;
 */
static void
add_sender_smallflow_arr(int flow_id){
    int ret = addSet(sender_smallflow_arr,flow_id,sender_flows);
    if(ret==-1){
        rte_panic("Error: %s\n", "start new flow get same flow id for small flow");
    }
    sender_small_flow_num_yog1++;
}

static void
recv_pkt(struct fwd_stream *fs);//已实现

//用于记录解析数据包包头得到的信息的结构体；
typedef struct  Parsing_info{
    uint16_t flow_id;
    uint32_t flow_size;
    uint16_t handing_flow_id;
    uint8_t  host_ctl_type;
    uint32_t flow_remain_size;
    uint32_t rank_in_sender;
    uint32_t receiver_reclen;
} Parsing_info;

static void
sender_send_pkt(void);


static Parsing_info* 
get_parse_info(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr);
//每次解析都要生成一个新的对象，但是要记得释放 
static Parsing_info*
get_parse_info(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr){
    Parsing_info* new_info = malloc(sizeof(Parsing_info));
    if(new_info==NULL){
        rte_panic("error, malloc info failed");
    }
    //解析所有控制数据包中的附带信息
    new_info->flow_id =  rte_be_to_cpu_16(transport_recv_hdr->FLOW_ID_16BITS);
    uint32_t flow_size_lowpart = (uint32_t)rte_be_to_cpu_16(transport_recv_hdr->FLOW_SIZE_LOW_16BITS);
    uint32_t flow_size_highpart = ((uint32_t)transport_recv_hdr->FLOW_SIZE_HIGH_16BITS << 16) & 0xffff0000;
    new_info->flow_size  = flow_size_highpart + flow_size_lowpart;
    new_info->handing_flow_id = (uint16_t)rte_be_to_cpu_16(ipv4_hdr->HANDLING_FLOW_ID_16BITS);
    new_info->rank_in_sender = rte_be_to_cpu_32(transport_recv_hdr->recv_ack);
    new_info->host_ctl_type = transport_recv_hdr->HOST_CTL_TYPE_8BIT;
    new_info->flow_remain_size = rte_be_to_cpu_32( transport_recv_hdr->FLOW_REMAIN_SIZE);

    uint8_t  pkt_type = transport_recv_hdr->PKT_TYPE_8BITS;
    if(pkt_type==PT_ACK)
        new_info->receiver_reclen  = rte_be_to_cpu_32(transport_recv_hdr->recv_ack);
    else    
        new_info->receiver_reclen = 0;
    return new_info;
}

static void
LOG_PKT_INFO(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr, const char *err_info)
{
    if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "%s\n", err_info);
    uint8_t pkt_type = transport_recv_hdr->PKT_TYPE_8BITS;
    Parsing_info *Par_info = get_parse_info(transport_recv_hdr, ipv4_hdr);
    if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "Type: %d, \n", pkt_type);
    if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "Body: flow_id = %d flow_size = %d handing_flow_id = %d host_ctl_type = %d flow_remain_size = %d rank_in_sender = %d receiver_reclen = %d\n", Par_info->flow_id, Par_info->flow_size, Par_info->handing_flow_id, Par_info->host_ctl_type, Par_info->flow_remain_size, Par_info->rank_in_sender, Par_info->receiver_reclen);
    free(Par_info);
}

static inline uint16_t
ip_sum(const unaligned_uint16_t *hdr, int hdr_len);

static void
construct_sync(int dst_server_id);

static void
construct_sync(int dst_server_id){
    struct   rte_ether_hdr *eth_hdr;
    struct   rte_ipv4_hdr *ip_hdr;
    struct   rte_tcp_hdr *transport_hdr;
    uint64_t ol_flags, tx_offloads;
    unsigned pkt_size = HDR_ONLY_SIZE;

    // if(DEBUG_LOCK_yog1) printf("[lock]construct_sync try to get lock\n");
    // rte_spinlock_lock(&send_lock_yog1);
    // if(DEBUG_LOCK_yog1) printf("[lock]construct_sync got lock\n");
    
    // struct rte_mempool *mbp = current_fwd_lcore()->mbp;
    struct rte_mbuf *pkt = rte_pktmbuf_alloc(send_pool_yog1);
    if (!pkt) {
        if(SHOW_DEBUG_INFO_yog1) printf("sync server %d: allocation pkt error", dst_server_id);
        rte_panic("sync server %d: allocation pkt error", dst_server_id);
    }

    rte_pktmbuf_reset(pkt);

    pkt->data_len = pkt_size;
    pkt->next = NULL;

    /* Initialize Ethernet header. */
    eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
    memset(eth_hdr, 0, L2_LEN);
    rte_ether_addr_copy(&eth_addr_array[dst_server_id], &eth_hdr->dst_addr);
    rte_ether_addr_copy(&eth_addr_array[this_server_id_yog1], &eth_hdr->src_addr);
    eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

    /* Initialize IP header. */
    ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
    memset(ip_hdr, 0, L3_LEN);
    ip_hdr->version_ihl     = IP_VHL_DEF;
    ip_hdr->type_of_service = 0;
    ip_hdr->fragment_offset = 0;
    ip_hdr->time_to_live    = IP_DEFTTL;
    ip_hdr->next_proto_id   = IPPROTO_TCP;
    ip_hdr->packet_id       = 0;
    ip_hdr->src_addr        = rte_cpu_to_be_32(ip_addr_array[this_server_id_yog1]);
    ip_hdr->dst_addr        = rte_cpu_to_be_32(ip_addr_array[dst_server_id]);
    ip_hdr->total_length    = RTE_CPU_TO_BE_16(pkt_size - L2_LEN);
    ip_hdr->hdr_checksum    = ip_sum((unaligned_uint16_t *)ip_hdr, L3_LEN);

    printf("will send to server %d, ip %u, eth %x\n", dst_server_id, ip_addr_array[dst_server_id], eth_addr_array[dst_server_id].addr_bytes[5]);
    printf("ip %u eth %x\n", ip_hdr->dst_addr, eth_hdr->dst_addr.addr_bytes[5]);

    /* Initialize transport header. */
    transport_hdr = (struct rte_tcp_hdr *)(ip_hdr + 1);
    transport_hdr->src_port       = 55555;
    transport_hdr->dst_port       = 55555;
    transport_hdr->sent_seq       = 0;
    transport_hdr->recv_ack       = 0;
    transport_hdr->PKT_TYPE_8BITS = PT_SYNC;
    
    // tx_offloads = ports[global_fs->tx_port].dev_conf.txmode.offloads;
    // if (tx_offloads & DEV_TX_OFFLOAD_VLAN_INSERT)
    //     ol_flags = PKT_TX_VLAN_PKT;
    // if (tx_offloads & DEV_TX_OFFLOAD_QINQ_INSERT)
    //     ol_flags |= PKT_TX_QINQ_PKT;
    // if (tx_offloads & DEV_TX_OFFLOAD_MACSEC_INSERT)
    //     ol_flags |= PKT_TX_MACSEC;

    // rte_pktmbuf_adj(pkt, (uint16_t)(pkt->pkt_len - pkt_size));
    pkt->nb_segs        = 1;
    pkt->data_len       = pkt_size;
    pkt->pkt_len        = pkt_size;
    // pkt->ol_flags       = ol_flags;
    // pkt->vlan_tci       = ports[global_fs->tx_port].tx_vlan_id;
    // pkt->vlan_tci_outer = ports[global_fs->tx_port].tx_vlan_id_outer;
    pkt->l2_len         = L2_LEN;
    pkt->l3_len         = L3_LEN;

    
    sender_pkts_burst[sender_current_burst_size_yog1] = pkt;
    sender_current_burst_size_yog1++;
    if (sender_current_burst_size_yog1 >= BURST_THRESHOLD) {
        sender_send_pkt();
        sender_current_burst_size_yog1 = 0;
    }
    // rte_spinlock_unlock(&send_lock_yog1);
    // if(DEBUG_LOCK_yog1) printf("[lock]construct_sync release lock\n");
}


static void 
record_new_flow_info(struct Parsing_info* Par_info,struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr);
//当首次加入接收端队列时，更新接收端信息，存储信息，为后续操作记录数据

static void 
record_new_flow_info(struct Parsing_info* Par_info,struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr){
    receiver_flows[Par_info->flow_id].src_port                = RTE_BE_TO_CPU_16(transport_recv_hdr->src_port);
    receiver_flows[Par_info->flow_id].dst_port                = RTE_BE_TO_CPU_16(transport_recv_hdr->dst_port);
    receiver_flows[Par_info->flow_id].src_ip                  = rte_be_to_cpu_32(ipv4_hdr->src_addr);
    receiver_flows[Par_info->flow_id].dst_ip                  = rte_be_to_cpu_32(ipv4_hdr->dst_addr);
    receiver_flows[Par_info->flow_id].start_time              = rte_rdtsc() / (double)hz;
    receiver_flows[Par_info->flow_id].flow_size               = Par_info->flow_size;
    receiver_flows[Par_info->flow_id].rank_in_sender          = Par_info->rank_in_sender;
    receiver_flows[Par_info->flow_id].flow_finished           = 0;
    receiver_flows[Par_info->flow_id].remain_size             = Par_info->flow_remain_size;
    

    if (receiver_flows[Par_info->flow_id].remain_size > sender_flows[Par_info->flow_id].flow_size) // should not happend
    {
        receiver_flows[Par_info->flow_id].remain_size = sender_flows[Par_info->flow_id].flow_size;
        if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"at info remain size error\n");
    }

    receiver_flows[Par_info->flow_id].receiver_recvlen        = 0;
    receiver_flows[Par_info->flow_id].receiver_finish_time    = 0;
    receiver_flows[Par_info->flow_id].unexcept_pkt_count      = 0;
}

static void 
new_firm_flow_arrivial(struct Parsing_info* Par_info);//未实现   李宇龙
/* 实现功能：
    0 检查该流在firm flow arr中的正确性（只出现一次，且存在）
    1 判断端侧状态是否会因为该流改变
        1-1 如果此时接收端空闲，正在接收流为空
            将该流标记为正在接收流
            传输grant packet  （send_grant_pkt  同时发2份）
            将端侧控制状态改为self_dst

        1-2 如果此时接收端正在接收流且正是该流
            将端侧控制状态改为self_dst
            返回Grant packet（send_grant_pkt）
        
        1-3 如果端侧控制是order_dst或者 端侧正在接收流减去半个RTTBYTE还是大于该流剩余流大小
            给正在接收流发送refuse（send_refuse_pkt 同时发2份）
            把正在接收流改为当前流
            给当前流发送Grant（send_grant_pkt）
            将端侧控制状态（ctl_type_dst）改为self_dst
 */

static void 
new_firm_flow_arrivial(struct Parsing_info* Par_info){
    if(!contains(receiver_firmflow_arr,Par_info->flow_id))//如果该流没存在过，报错推出
        rte_panic("error with new firm flow arr not have flow id");
    if(receiving_flow_id_yog1==0){//如果没有正在接收流
        RTE_LOG(DEBUG, ACL, "接收端id=%d 没有正在接收流,所以接收流id = %d", this_server_id_yog1, Par_info->flow_id);
        if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "接收端id=%d 没有正在接收流,所以接收流id = %d\n", this_server_id_yog1, Par_info->flow_id);
        receiving_flow_id_yog1 = Par_info->flow_id;   // 标记该流为正在接收流
        receiver_ctl_type_yog1 = self_dst;            // 改变端侧状态
        send_ctl_pkt(PT_GRANT, Par_info->flow_id, 1); // 发送grant
    }
    else if(receiving_flow_id_yog1==Par_info->flow_id){
        RTE_LOG(DEBUG, ACL, "接收端id=%d 正在接收流 = %d 与firm 流一致,修改端侧状态,发送grant", this_server_id_yog1, Par_info->flow_id);
        if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "接收端id=%d 正在接收流 = %d 与firm 流一致,修改端侧状态,发送grant\n", this_server_id_yog1, Par_info->flow_id);
        if (receiver_ctl_type_yog1 != order_dst)
        {
            if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "接收端id=%d 正在接收流 = %d 与firm 流一致,修改端侧状态,但是端侧状态与预期不符为：%d\n", this_server_id_yog1, Par_info->flow_id, receiver_ctl_type_yog1);
            RTE_LOG(WARNING, ACL, "接收端id=%d 正在接收流 = %d 与firm 流一致,修改端侧状态,但是端侧状态与预期不符为：%d", this_server_id_yog1, Par_info->flow_id, receiver_ctl_type_yog1);
        }
        receiver_ctl_type_yog1 = self_dst;            // 修改端侧状态
        send_ctl_pkt(PT_GRANT, Par_info->flow_id, 1); // 发送grant
    }
    else if(receiver_ctl_type_yog1==order_dst || Par_info->flow_remain_size+RTT_BYTES<receiver_flows[receiving_flow_id_yog1].remain_size)
    {
        if(receiver_ctl_type_yog1==order_dst)//如果是因为端侧为order
        {
            RTE_LOG(DEBUG, ACL, "接收端id=%d 传输order 流   原本流为%d  新流为%d",this_server_id_yog1,receiving_flow_id_yog1,Par_info->flow_id);
        }
        else{//如果是self但是流大
            RTE_LOG(DEBUG, ACL, "接收端id=%d 传输的流比较大  原本流为%d  新流为%d",this_server_id_yog1,receiving_flow_id_yog1,Par_info->flow_id); 
        }
        int refuse_flow_id = receiving_flow_id_yog1;
        receiver_ctl_type_yog1=self_dst;//更新端侧状态
        receiving_flow_id_yog1 = Par_info->flow_id;//标记该流
        send_ctl_pkt(PT_REFUSE,refuse_flow_id,1);//发送拒绝包
        send_ctl_pkt(PT_GRANT,Par_info->flow_id,1);//发送grant
    }
}

static void
recv_info(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr);//未实现   李宇龙 
/* 实现功能：
    0 读取info数据包记录的信息用以完善 receiver_flows 数组中的必须内容
    1 加入接收端的活跃流队列（reciver_actflow_arr(未定义)） 利用单独函数实现，保证该队列中流ID只出现一次，不必保证有序，并记录接收端的信息。
    2 读取流排名信息，并将发送端RANK=1的流单独加入有序队列（Firm_flow_arr（未定义））,保证流ID只出现一次，且按照flow_remian_size大小排序
    3 如果该流rank=1,调用函数new_firm_flow_arrivial
 */

static void
recv_info(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr)
{
    Parsing_info *Par_info = get_parse_info(transport_recv_hdr, ipv4_hdr);
    if(get_dst_server_id(Par_info->flow_id,sender_flows)!=this_server_id_yog1)
    {
        LOG_PKT_INFO(transport_recv_hdr,ipv4_hdr,"[Error]pase info error at info\n");
        free(Par_info);
        return;
    }
    int ret = contains(receiver_actflow_arr, Par_info->flow_id);
    if (ret != 1)
    {
        record_new_flow_info(Par_info, transport_recv_hdr, ipv4_hdr);
        ret = addSet(receiver_actflow_arr, Par_info->flow_id, receiver_flows);
        if(ret == 1)
            receiver_active_flow_num_yog1++;
        else
            LOG_PKT_INFO(transport_recv_hdr, ipv4_hdr, "[Error][recv_info]addSet failed!\n");
    } // 加入接收端活跃队列，成功加入后更新全局统计数值

    if (Par_info->rank_in_sender == 1)
    {                                                                                // 如果时firm flow
        ret = addSet(receiver_firmflow_arr, Par_info->flow_id, receiver_flows); // 加入列表
        if (ret == 1)
            receiver_firm_flow_num_yog1++; // 如果没存在过，更新统计数据
        new_firm_flow_arrivial(Par_info);  // 尝试对端侧进行修改
    }
    free(Par_info);
}

static void
recv_grant(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr);//未实现    应锦特
/* 实现功能：
    首先判断该流在发送端是否还是最小流，如果不是，那么调用函数发送refuse，结束
    如果是：
        1 如果该发送端是空闲的（sending_flow是空的）
            该发送端正在发送流改为该流
            发送端ctl_type_src改为self_src
        2 如果发送端不是空闲的：
            如果该流id与正在发送流id相同：
                端侧状态改为self_src
            如果不同
                对正在发送流发送refuse_pkt
                正在发送流改为该流
                端侧状态改为self_src
 */

static void
recv_grant(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr)
{
    Parsing_info *Par_info = get_parse_info(transport_recv_hdr, ipv4_hdr);
    if(get_src_server_id(Par_info->flow_id,sender_flows)!=this_server_id_yog1)
    {
        LOG_PKT_INFO(transport_recv_hdr,ipv4_hdr,"[Error]pase info error at grant\n");
        free(Par_info);
        return;
    }
    if (sender_flows[Par_info->flow_id].rank_in_sender != 1)
    {                                                  // 如果读取信息不是发送端rank=1
        send_ctl_pkt(PT_REFUSE, Par_info->flow_id, 1); // 发送拒绝包拒绝该流grant
        free(Par_info);
        return;
    }
    if (sending_flow_id_yog1 == 0)
    { // 如果没有发送端，标记该流并修改端侧状态
        sending_flow_id_yog1 = Par_info->flow_id;
        sender_ctl_type_yog1 = self_src;
    }
    else
    {
        if (sending_flow_id_yog1 == Par_info->flow_id)
        { // 如果已经发送该流了，可能是接收端更新发送端状态
            sender_ctl_type_yog1 = self_src;
        }
        else
        { // 如果不是该流，那么就要接收该流的grant，因为grant>order
            int refuse_flow_id = sending_flow_id_yog1;
            sending_flow_id_yog1 = Par_info->flow_id; //
            sender_ctl_type_yog1 = self_src;
            send_ctl_pkt(PT_REFUSE, refuse_flow_id, 1); // 发送拒绝包,但是信息要是最新的！！！！！！！！！！
        }
    }
    free(Par_info);
}

static void
host_change_flow(void);//未实现    姚易男
/* 实现功能：
    尝试在firm Flow的列表中查找最小流：
        如果列表大小为0 直接返回
    如果不为0
        找到最小流ID
        对该流发送grant_pkt
        标记该流为正在接收流
        标记接收端状态为self_dst
 */

static void
host_change_flow(void){
    if(receiver_firm_flow_num_yog1>0 && getset_smallest_flowid(receiver_firmflow_arr)==-1){
        RTE_LOG(WARNING, ACL, "receiver %d  firm num>0 but not in arr",this_server_id_yog1);
        if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "receiver %d  firm num>0 but not in arr",this_server_id_yog1);
    }
    if(receiver_firm_flow_num_yog1<=0 && getset_smallest_flowid(receiver_firmflow_arr)!=-1){
        RTE_LOG(WARNING, ACL, "receiver %d  firm num==0 but in arr",this_server_id_yog1);
        if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "receiver %d  firm num>0 but not in arr",this_server_id_yog1);
    }
    int firm_flow_id = getset_smallest_flowid(receiver_firmflow_arr);//得到最小流id
    if(firm_flow_id==-1)    return;
    if(receiver_flows[firm_flow_id].remain_size <= 0)
    {
        fprintf(fp, "[Error][host_change_flow]smallest firm flow remain size is 0, but still in list.\n");
        removeElement(receiver_firmflow_arr, firm_flow_id);
        firm_flow_id = getset_smallest_flowid(receiver_firmflow_arr);
    }
    if(firm_flow_id==-1)    return;
    receiving_flow_id_yog1 = firm_flow_id;//修改端侧状态
    receiver_ctl_type_yog1 = self_dst;
    send_ctl_pkt(PT_GRANT,firm_flow_id,1);//发送控制包
}

static void
receiver_flow_finish(Parsing_info* Par_info);

static void
receiver_flow_finish(Parsing_info* Par_info){
    if(!receiver_flows[Par_info->flow_id].flow_finished)
        receiver_finished_flow_num_yog1++;
    receiver_flows[Par_info->flow_id].receiver_finish_time = rte_rdtsc() / (double)hz;//更改端侧信息
    receiver_flows[Par_info->flow_id].flow_finished        = 1;
    if(receiver_flows[Par_info->flow_id].flow_size<=RTT_BYTES){//如果是个小流，抹除记录后结束
        int ret = removeElement(receiver_smallflow_arr,Par_info->flow_id);
        if(SHOW_DEBUG_INFO_yog1) printf("will send flow finish in L1046\n");
        send_ctl_pkt(PT_FLOW_FINISH,Par_info->flow_id,0);//发送流结束数据包,应该最后发送，保证信息最新
        if(ret==-1)
            RTE_LOG(WARNING, ACL, "接收端 %d, 小流结束,id  =%d 但是没有记录过数据",this_server_id_yog1,Par_info->flow_id);
        return;
    }
    else{//如果是大流
        int ret = removeElement(receiver_actflow_arr,Par_info->flow_id);//移除活跃流表
        if(ret==-1) //如果没有正常移除，报错
            RTE_LOG(WARNING, ACL, "接收端 %d, 大流结束,id  =%d 但是没有记录过act数据",this_server_id_yog1,Par_info->flow_id);
        else    //正常移除，减去活跃流数目
            receiver_active_flow_num_yog1--;
        if(receiver_flows[Par_info->flow_id].rank_in_sender==1){
            ret = removeElement(receiver_firmflow_arr,Par_info->flow_id);//如果是firm flow
            if(ret==-1){
                  RTE_LOG(WARNING, ACL, "接收端 %d, 大流结束,id  =%d 但是没有记录过firm数据",this_server_id_yog1,Par_info->flow_id);
            }
            else    
                receiver_firm_flow_num_yog1--;
        }
        if(receiving_flow_id_yog1==Par_info->flow_id){
            //如果是正在接收流
            receiver_ctl_type_yog1=unsure;//清除端侧状态
            receiving_flow_id_yog1=0;//
        }
        if(SHOW_DEBUG_INFO_yog1) printf("will send flow finish in L1070\n");
        send_ctl_pkt(PT_FLOW_FINISH,Par_info->flow_id,1);//发送流结束数据包,应该最后发送，保证信息最新
        if(receiver_ctl_type_yog1==unsure && receiving_flow_id_yog1<=0){//如果端侧没有正在发送流
            host_change_flow();//尝试去grant新的流
        }
    }
}

static void
recv_data(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr);//未实现   应锦特
/* 实现功能：
    收到数据包后首先更新接收端流状态，收到info后构建的那个reciver_flows
    返回ACK  用发送控制包的API

    如果该流的flow_size小于等于1个BDP
        如果正在接收流不是空的：
            那么给正在发送流返回一个wait pkt

    如果该流的flow_size大于1个BDP且不是正在发送流或者如果接收端是空的
        那么就发送一个refuse pkt，并可以接收1个BDP大小的该流，如果该流超过BDP还未停止，就再发一次refuse

    如果该流小于等于1BDP，记录流信息与接收信息后返回
    如果该流大于1BDP
        如果该流在接收端结束，发送flow_finish到发送端与集中控制器
        端侧标记该流结束
        端侧把该流在活跃流列表actflow_arr中删除
    如果该流在发送端的排名为1
        那么删除该流在firm FLow列表中
    如果该流是正在接收流：
        先将端侧状态改为unsure
        然后将正在接收流置为不存在
    那么调用host_change_flow（）用以尝试选择新流接收
 */

static void
recv_data(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr)
{
    Parsing_info *Par_info = get_parse_info(transport_recv_hdr, ipv4_hdr);
    if(get_dst_server_id(Par_info->flow_id,sender_flows)!=this_server_id_yog1)
    {
        LOG_PKT_INFO(transport_recv_hdr,ipv4_hdr,"[Error]parse info error at data\n");
        free(Par_info);
        return;
    }
    if(SHOW_DEBUG_INFO_yog1) printf("recv data from %d flowid = %d\n", get_src_server_id(Par_info->flow_id, sender_flows), Par_info->flow_id);
    if (Par_info->flow_size <= RTT_BYTES)
    { // 如果是个小流
        if (!contains(receiver_smallflow_arr, Par_info->flow_id))
        {                                                                           // 如果第一次到达
            record_new_flow_info(Par_info, transport_recv_hdr, ipv4_hdr);           // 并记录流信息
            addSet(receiver_smallflow_arr, Par_info->flow_id, receiver_flows); // 加入小流列表
        }
        // if (receiving_flow_id_yog1 != 0) // 如果正在接收流不是空的，那么就要对该流发送wait
        //     send_ctl_pkt(PT_WAIT, receiving_flow_id_yog1, 0);
    }
    if (!contains(receiver_actflow_arr, Par_info->flow_id) && !contains(receiver_smallflow_arr, Par_info->flow_id))
    {
        RTE_LOG(WARNING, ACL, "host id = %d recv flow id = %d data with out record", this_server_id_yog1, Par_info->flow_id);
    } // 如果哪个列表都不存在，就要报warning

    receiver_flows[Par_info->flow_id].remain_size -= (DEFAULT_PKT_SIZE - HDR_ONLY_SIZE);
    if (receiver_flows[Par_info->flow_id].remain_size < 0)
        receiver_flows[Par_info->flow_id].remain_size = 0;
    receiver_flows[Par_info->flow_id].receiver_recvlen += (DEFAULT_PKT_SIZE - HDR_ONLY_SIZE); // G更新记录的流信息
    send_ctl_pkt(PT_ACK, Par_info->flow_id, 0);
    if (Par_info->flow_size > RTT_BYTES)
    // {                                                                              // 如果是个小流
    //     updateSet(receiver_smallflow_arr, Par_info->flow_id, receiver_flows); // 保证链表有序
    // }
    // else
    {
        if (contains(receiver_firmflow_arr, Par_info->flow_id))
            updateSet(receiver_firmflow_arr, Par_info->flow_id, receiver_flows);
    }
    if(Par_info->flow_id==receiving_flow_id_yog1){
        if(receiver_flows[Par_info->flow_id].remain_size<=RTT_BYTES && sender_flows[Par_info->flow_id].is_pre_grant==0){
            
            int ret = removeElement(receiver_firmflow_arr,receiving_flow_id_yog1);
            int new_flow = getset_smallest_flowid(receiver_firmflow_arr);
            if(new_flow>0){
                send_ctl_pkt(PT_GRANT,new_flow,0);
                sender_flows[Par_info->flow_id].is_pre_grant = 1;
            }
            if(ret!=-1)
                addSet(receiver_firmflow_arr,receiving_flow_id_yog1,receiver_flows);
                
        }
    }

    // if (sender_flows[Par_info->flow_id].flow_size > RTT_BYTES && (receiving_flow_id_yog1 == 0 || receiving_flow_id_yog1 != Par_info->flow_id))
    // { // 如果接收到的是个大流且不是正在接收流
    //     if (receiver_flows[Par_info->flow_id].unexcept_pkt_count == 0)
    //     {
    //         receiver_flows[Par_info->flow_id].unexcept_pkt_count = RTT_BYTES / (DEFAULT_PKT_SIZE - HDR_ONLY_SIZE);
    //         send_ctl_pkt(PT_REFUSE, Par_info->flow_id, 1);
    //     }
    //     else
    //     {
    //         receiver_flows[Par_info->flow_id].unexcept_pkt_count--;
    //     }
    // }
    // 流结束相关操作：
    if (receiver_flows[Par_info->flow_id].remain_size <= 0)
    {
        receiver_flow_finish(Par_info);
    }
    free(Par_info);
}

static void
recv_ack(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr);//未实现    应锦特
/* 实现功能：
    首先更新接收流信息(看info里面需要更新的)
    如果流结束(收到了足够的数据包)
        如果该流是小于1BDP流，那就直接结束，发送端删除该流信息
        如果不是
            发送端状态修改为unsure
            发送端删除该流信息（发送端存储的该流信息，表中的，不要删除sender_flows）
            更新最小流信息，并对最小流传输change_rank
        返回
    然后由于接收到ACK后流剩余大小减小，需要判断该流如果是正在发送流且不是接收端最小流
        如果剩余流大小成为最小的流，那么就要向接收端发送change_rank包
        其中包括正在发送流的rank=1与原本最小流的rank=2
        修改最小流
        需要判断最小流标准
 */
static void
recv_ack(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr){
    //更新流相关信息
    Parsing_info* Par_info = get_parse_info(transport_recv_hdr,ipv4_hdr);
    if(get_src_server_id(Par_info->flow_id,sender_flows)!=this_server_id_yog1)
    {
        LOG_PKT_INFO(transport_recv_hdr,ipv4_hdr,"[Error]pase info error at ack\n");
        return;
    }

    if(SHOW_DEBUG_INFO_yog1) printf("recv ack from %d flowid = %d \n", get_dst_server_id(Par_info->flow_id, sender_flows), Par_info->flow_id);
    if(Par_info->receiver_reclen>sender_flows[Par_info->flow_id].sender_acklen)
        sender_flows[Par_info->flow_id].sender_acklen = Par_info->receiver_reclen;
    sender_flows[Par_info->flow_id].remain_size = sender_flows[Par_info->flow_id].flow_size-sender_flows[Par_info->flow_id].sender_acklen;
    if(sender_flows[Par_info->flow_id].remain_size < 0) sender_flows[Par_info->flow_id].remain_size = 0;
    if(sender_flows[Par_info->flow_id].remain_size>0)//该流还没结束
    {
        if(sender_flows[Par_info->flow_id].flow_size>RTT_BYTES)
            updateSet(sender_largeflow_arr,Par_info->flow_id,sender_flows);
        else
            updateSet(sender_smallflow_arr,Par_info->flow_id,sender_flows);
    }
    //如果该流结束了
    if(sender_flows[Par_info->flow_id].sender_acklen>=sender_flows[Par_info->flow_id].flow_size){
        //记录信息
        if(!sender_flows[Par_info->flow_id].flow_finished)
            sender_finished_flow_num_yog1++;
        sender_flows[Par_info->flow_id].flow_finished = 1;
        sender_flows[Par_info->flow_id].sender_finish_time = rte_rdtsc() / (double)hz;
        //小流删除后返回
        if(sender_flows[Par_info->flow_id].flow_size<=RTT_BYTES)
        {
            removeElement(sender_smallflow_arr,Par_info->flow_id);
        }//大流更改端侧状态
        else{
            //把端侧状态进行修改
            sender_ctl_type_yog1 = unsure;
            if(Par_info->flow_id==sending_flow_id_yog1)
                sending_flow_id_yog1 = 0;
            if(Par_info->flow_id==sender_smallest_flow_id_yog1)
                sender_smallest_flow_id_yog1 = 0;
            removeElement(sender_largeflow_arr,Par_info->flow_id);
            //找到新的最小流并传输控制包
            int smallest_id = getset_smallest_flowid(sender_largeflow_arr);
            if(smallest_id!=-1){
                if(sender_smallest_flow_id_yog1 > 0 && smallest_id!=sender_smallest_flow_id_yog1)
                {
                    sender_flows[sender_smallest_flow_id_yog1].rank_in_sender = 2;
                    send_ctl_pkt(PT_CHANGE_RANK_2,sender_smallest_flow_id_yog1,1);
                }
                sender_smallest_flow_id_yog1 = smallest_id;
                sender_flows[sender_smallest_flow_id_yog1].rank_in_sender = 1;
                send_ctl_pkt(PT_CHANGE_RANK_1,sender_smallest_flow_id_yog1,1);
            }
        }
        
    }

    //如果该流没结束
    else{
        //如果是正在发送流且不是端侧最小流
        if(Par_info->flow_id==sending_flow_id_yog1 && Par_info->flow_id!=sender_smallest_flow_id_yog1){
            int smallest_id = getset_smallest_flowid(sender_largeflow_arr);
            if(smallest_id==-1)
                rte_panic("rec ack get error smallest flow id");
            if(smallest_id==Par_info->flow_id){
                sender_flows[sender_smallest_flow_id_yog1].rank_in_sender = 2;
                send_ctl_pkt(PT_CHANGE_RANK_2,sender_smallest_flow_id_yog1,1);
                sender_smallest_flow_id_yog1 = smallest_id;
                sender_flows[sender_smallest_flow_id_yog1].rank_in_sender = 1;
                send_ctl_pkt(PT_CHANGE_RANK_1,sender_smallest_flow_id_yog1,1);
            }
        }
    }

    free(Par_info);

}

static void
recv_change_rank1(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr);//未实现   姚易男
/* 实现功能：
    先将该流加入firm Flow的数组中（需要正确性检查）
    然后new_firm_flow_arrivial
 */
static void
recv_change_rank1(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr){
    Parsing_info* Par_info = get_parse_info(transport_recv_hdr,ipv4_hdr);
    if(get_dst_server_id(Par_info->flow_id,sender_flows)!=this_server_id_yog1)
    {
        LOG_PKT_INFO(transport_recv_hdr,ipv4_hdr,"[Error]pase info error at c1\n");
        return;
    }
    receiver_flows[Par_info->flow_id].rank_in_sender = 1;
    addSet(receiver_firmflow_arr,Par_info->flow_id,receiver_flows);
    new_firm_flow_arrivial(Par_info);
    free(Par_info);
}
static void
recv_change_rank2(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr);//未实现  姚易男
/* 实现功能：
    那么首先要将该流从firm flow数组中删除（需要正确性检查）
    如果该流是接收端正在接收流且端侧状态时self_dst
        给正在接收流发送refuse packet
        端侧recving flow清零
        端侧状态改为unsure
    尝试寻找一个新的firm flow，找firm flow arr中的最小的
    如果找到了新的firm FLOW:
        标记新流为正在发送流
        标记端侧为self_dst
        发送grant
 */

static void
recv_change_rank2(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr){
    Parsing_info* Par_info = get_parse_info(transport_recv_hdr,ipv4_hdr);
    if(get_dst_server_id(Par_info->flow_id,sender_flows)!=this_server_id_yog1)
    {
        LOG_PKT_INFO(transport_recv_hdr,ipv4_hdr,"[Error]pase info error at c2\n");
        free(Par_info);
        return;
    }
    receiver_flows[Par_info->flow_id].rank_in_sender = 2;
    removeElement(receiver_firmflow_arr,Par_info->flow_id);
    
    if(Par_info->flow_id==receiving_flow_id_yog1){
        receiving_flow_id_yog1 = 0;
        receiver_ctl_type_yog1 = unsure;
        send_ctl_pkt(PT_REFUSE,Par_info->flow_id,1);
    }
    host_change_flow();
    free(Par_info);
}



static void
recv_refuse(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr);//未实现   姚易男
/* 实现功能：
    利用get_src_server_id 函数判断端侧是发送端还是接收端
    如果是发送端接收到refuse：
        检查是否与正在发送流相同，如果不同就忽略
        如果相同，修改发送端正在发送流为空  发送端端侧状态修改为unsure
    如果接收端接收到refuse：
        检查是否与正在接收流相同，如果不同就忽略
        如果相同，修改接收端正在接收流为空  接收端端端侧状态修改为unsure
        调用host_change_flow()查看是否能找到新流发送
 */
static void
recv_refuse(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr)
{
    Parsing_info *Par_info = get_parse_info(transport_recv_hdr, ipv4_hdr);
    if (get_src_server_id(Par_info->flow_id, sender_flows) == this_server_id_yog1) // 端侧为发送端
    {
        if (Par_info->flow_id == sending_flow_id_yog1)
        {
            sending_flow_id_yog1 = 0;
            sender_ctl_type_yog1 = unsure;
        }
    }
    else // 端侧为接收端
    {
        if (Par_info->flow_id == receiving_flow_id_yog1)
        {
            receiving_flow_id_yog1 = 0;
            receiver_ctl_type_yog1 = unsure;
            int ret = removeElement(receiver_firmflow_arr, Par_info->flow_id);
            if (ret == -1)
                if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "recv_refuse remove flowid error\n");
            host_change_flow();
            if (ret != -1)
                addSet(receiver_firmflow_arr, Par_info->flow_id, receiver_flows);
        }
    }
    free(Par_info);
}

static void
recv_wait(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr);//未实现   范子奇
/* 实现功能：
    正确性检查，是否为该端点的流
    检查流是否为正在发送流
        如果是则流wait计数加一，用于延缓发送数据包
 */
static void
recv_wait(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr){
    Parsing_info* Par_info = get_parse_info(transport_recv_hdr,ipv4_hdr);
    if(get_src_server_id(Par_info->flow_id,sender_flows)!=this_server_id_yog1)
    {
        LOG_PKT_INFO(transport_recv_hdr,ipv4_hdr,"[Error]pase info error at wait\n");
        return;
    }
    if(Par_info->flow_id==sending_flow_id_yog1){
        sender_flows[Par_info->flow_id].sender_wait_count++;
    }
    free(Par_info);
}

static void
recv_order(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr);//未实现   范子奇
/* 实现功能：
    利用get_src_server_id 函数判断端侧是发送端还是接收端
    如果是接收端：
        首先检查端侧状态，如果端侧是self_dst,直接拒绝该order，向该流发送端发送refuse
        如果端侧状态不是self_dst:
            修改端侧状态为order_dst
            如果有正在接收流，向正在接收流发送refuse
            将order流改为正在接收流
    如果是发送端：
        首先检查端侧状态，如果端侧是self_src,直接拒绝该order，向该流接收端发送refuse
        如果端侧状态不是self_src:
            修改端侧状态为order_src
        如果有正在接收流，向正在发送流发送refuse
            将order流改为正在发送流
 */

static void
recv_order(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr){
    Parsing_info* Par_info = get_parse_info(transport_recv_hdr,ipv4_hdr);
    if(get_src_server_id(Par_info->flow_id,sender_flows)==this_server_id_yog1)//端侧为发送端
    {
        if(sending_flow_id_yog1==Par_info->flow_id)
            return;
        if(sender_ctl_type_yog1==self_src){
            if(sending_flow_id_yog1==Par_info->flow_id)
                return;
            send_ctl_pkt(PT_REFUSE,Par_info->flow_id,1);
        }
        else{
            sender_ctl_type_yog1=order_src;
            int refuse_id = 0;//暂存记录
            if(sending_flow_id_yog1>0)
                refuse_id = sending_flow_id_yog1;
            sending_flow_id_yog1 = Par_info->flow_id;
            if(refuse_id>0)
                send_ctl_pkt(PT_REFUSE,refuse_id,1);
        }
    }
    else{//端侧为接收端
        if(receiving_flow_id_yog1==Par_info->flow_id)
            return;
        if(receiver_ctl_type_yog1==self_dst){
            if(receiving_flow_id_yog1==Par_info->flow_id)
                return;
            send_ctl_pkt(PT_REFUSE,Par_info->flow_id,1);
        }
        else{
            receiver_ctl_type_yog1=order_dst;
            int refuse_id = 0;//暂存记录
            if(receiving_flow_id_yog1>0)
                refuse_id = receiving_flow_id_yog1;
            receiving_flow_id_yog1 = Par_info->flow_id;
            if(refuse_id>0)
                send_ctl_pkt(PT_REFUSE,refuse_id,1);
        }
    }
    free(Par_info);
}


static void
recv_flow_finish(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr);//未实现  范子奇
/* 实现功能：
    如果是端侧接收到flow_finish（flow_finish里面最好带有接收端的最新状态，就是更新完再发）
    检查发送端该流是否结束，如果没结束就结束，结束了就忽略
 */

static void
recv_flow_finish(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr){
    Parsing_info* Par_info = get_parse_info(transport_recv_hdr,ipv4_hdr);
    printf("Flow id: %d finished.\n", Par_info->flow_id);
    if(get_dst_server_id(Par_info->flow_id,sender_flows)==this_server_id_yog1)
    {
        receiver_flow_finish(Par_info);
        return;
    }

    if(sender_flows[Par_info->flow_id].flow_finished!=1){
        //记录信息
        sender_flows[Par_info->flow_id].flow_finished = 1;
        sender_finished_flow_num_yog1++;
        if(sender_flows[Par_info->flow_id].sender_unack_size > 0)
        {
            if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "[Warn]recv flow_finish but sender_unack_size > 0, will clear it anyway.\n");
            if(SHOW_DEBUG_INFO_yog1) printf("[Warn]recv flow_finish but sender_unack_size > 0, will clear it anyway.\n");
            sender_flows[Par_info->flow_id].sender_unack_size = 0;
            sender_flows[Par_info->flow_id].sender_can_send_size = 0;
        }
        sender_flows[Par_info->flow_id].sender_finish_time = rte_rdtsc() / (double)hz;
        //小流删除后返回
        if(sender_flows[Par_info->flow_id].flow_size<=RTT_BYTES)
        {
            removeElement(sender_smallflow_arr,Par_info->flow_id);
        }//大流更改端侧状态
        else{
            //把端侧状态进行修改
            sender_ctl_type_yog1 = unsure;
            if(Par_info->flow_id==sending_flow_id_yog1)
                sending_flow_id_yog1 = 0;
            if(Par_info->flow_id==sender_smallest_flow_id_yog1)
                sender_smallest_flow_id_yog1 = 0;
            removeElement(sender_largeflow_arr,Par_info->flow_id);
            //找到新的最小流并传输控制包
            int smallest_id = getset_smallest_flowid(sender_largeflow_arr);
            if(smallest_id!=-1){
                if(sender_smallest_flow_id_yog1>0 && smallest_id!=sender_smallest_flow_id_yog1)
                {
                    sender_flows[sender_smallest_flow_id_yog1].rank_in_sender = 2;
                    send_ctl_pkt(PT_CHANGE_RANK_2,sender_smallest_flow_id_yog1,1);
                }
                sender_smallest_flow_id_yog1 = smallest_id;
                sender_flows[sender_smallest_flow_id_yog1].rank_in_sender = 1;
                send_ctl_pkt(PT_CHANGE_RANK_1,sender_smallest_flow_id_yog1,1);
            }
        }
    }

    

    free(Par_info);
}

static void recv_sync(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr)
{
    Parsing_info* Par_info = get_parse_info(transport_recv_hdr,ipv4_hdr);

    int server = rte_be_to_cpu_32(ipv4_hdr->src_addr) & 0xff;
    // printf("recv sync from server %d\n", server);
    sync_count = sync_count | (1 << (server-1));
    printf("%x, %x\n", sync_count, MASK);
    if(sync_count == MASK)
    {
        printf("sync done\n");
        sync_done_yog1 = 1;
    }

    free(Par_info);
}

// static int
// get_sender_can_send_small_flow_id(Set* set);//获取发送端可以发送的小流id

// static int
// get_sender_can_send_small_flow_id(Set* set){
//     if(sender_small_flow_num_yog1>0){
//         SetNode* current_node = set->head;

//     while (current_node != NULL && current_node->flow_id!=flow_id) {
//         current_node = current_node->next;
//     }
//     }
// }




static void
construct_data(uint32_t flow_id)
{
    struct   rte_ether_hdr *eth_hdr;
    struct   rte_ipv4_hdr *ip_hdr;
    struct   rte_tcp_hdr *transport_hdr;
    uint64_t ol_flags, tx_offloads;
    uint16_t data_len;
    int      dst_server_id;
    unsigned pkt_size;

    // if(DEBUG_LOCK_yog1) printf("[lock]construct_data try to get lock\n");
    // rte_spinlock_lock(&send_lock_yog1);
    // if(DEBUG_LOCK_yog1) printf("[lock]construct_data got lock\n");

    // printf("flow id = %d send data\n",flow_id);
    // struct rte_mempool *mbp = current_fwd_lcore()->mbp;
    struct rte_mbuf *pkt = rte_mbuf_raw_alloc(send_pool_yog1);
    if (!pkt) {
        if(SHOW_DEBUG_INFO_yog1) printf("flow_id = %d: allocation pkt error", flow_id);
        rte_panic("flow_id = %d: allocation pkt error", flow_id);
    }

    pkt_size = DEFAULT_PKT_SIZE;
    pkt->data_len = pkt_size;
    pkt->next = NULL;

    /* Initialize Ethernet header. */
    eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
    dst_server_id = get_dst_server_id(flow_id, sender_flows);
    if (dst_server_id == -1) {
        if(SHOW_DEBUG_INFO_yog1) printf("server error: cannot find server id\n");
	// if(SHOW_DEBUG_INFO_yog1) printf("%d",flow_id);
    }
    rte_ether_addr_copy(&eth_addr_array[dst_server_id], &eth_hdr->dst_addr);
    rte_ether_addr_copy(&eth_addr_array[this_server_id_yog1], &eth_hdr->src_addr);
    eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

    /* Initialize IP header. */
    ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
    memset(ip_hdr, 0, L3_LEN);
    ip_hdr->version_ihl     = IP_VHL_DEF;
    if(sender_flows[flow_id].flow_size>RTT_BYTES)
        ip_hdr->type_of_service = (uint8_t)(0x00 << 2);
    else
        ip_hdr->type_of_service = (uint8_t)(0x01 << 2);
    ip_hdr->fragment_offset = 0;
    ip_hdr->time_to_live    = IP_DEFTTL;
    ip_hdr->next_proto_id   = IPPROTO_TCP;
    ip_hdr->packet_id       = 0;
    ip_hdr->src_addr        = rte_cpu_to_be_32(ip_addr_array[this_server_id_yog1]);
    ip_hdr->dst_addr        = rte_cpu_to_be_32(sender_flows[flow_id].dst_ip);
    // if(SHOW_DEBUG_INFO_yog1) printf("流目的地址 = %d 源地址 = %d\n",ip_hdr->dst_addr,ip_hdr->src_addr);
    ip_hdr->total_length    = RTE_CPU_TO_BE_16(pkt_size - L2_LEN);
    ip_hdr->hdr_checksum    = ip_sum((unaligned_uint16_t *)ip_hdr, L3_LEN);
    /* Aeolus enables selective dropping for unscheduled data pkts */
    
    /* Initialize transport header. */
    transport_hdr = (struct rte_tcp_hdr *)(ip_hdr + 1);
    transport_hdr->src_port       = rte_cpu_to_be_16(sender_flows[flow_id].src_port);
    transport_hdr->dst_port       = rte_cpu_to_be_16(sender_flows[flow_id].dst_port);
    transport_hdr->PKT_TYPE_8BITS = PT_DATA;
    transport_hdr->FLOW_ID_16BITS = rte_cpu_to_be_16((uint16_t)(flow_id & 0xffff));
    transport_hdr->FLOW_SIZE_LOW_16BITS  = rte_cpu_to_be_16((uint16_t)(sender_flows[flow_id].flow_size & 0xffff));
    transport_hdr->FLOW_SIZE_HIGH_16BITS = (uint16_t)((sender_flows[flow_id].flow_size >> 16) & 0xffff);
    transport_hdr->FLOW_REMAIN_SIZE      = rte_cpu_to_be_32(sender_flows[flow_id].remain_size);
    transport_hdr->recv_ack              = rte_cpu_to_be_32(sender_flows[flow_id].rank_in_sender);
    data_len = (pkt_size - HDR_ONLY_SIZE);
    if (data_len > sender_flows[flow_id].sender_can_send_size) {
        data_len = sender_flows[flow_id].sender_can_send_size;
        pkt_size = HDR_ONLY_SIZE + data_len;
    }
    // transport_hdr->DATA_LEN_16BITS = RTE_CPU_TO_BE_16(data_len);
    sender_flows[flow_id].sender_unack_size += data_len;
    sender_flows[flow_id].sender_can_send_size -= data_len;

    myLinkedListAddAtTail(flow_id,sender_flows[flow_id].sender_unack_size,data_len,rte_rdtsc() / (double)hz);
    tx_offloads = ports[global_fs->tx_port].dev_conf.txmode.offloads;
    if (tx_offloads & DEV_TX_OFFLOAD_VLAN_INSERT)
        ol_flags = PKT_TX_VLAN_PKT;
    if (tx_offloads & DEV_TX_OFFLOAD_QINQ_INSERT)
        ol_flags |= PKT_TX_QINQ_PKT;
    if (tx_offloads & DEV_TX_OFFLOAD_MACSEC_INSERT)
        ol_flags |= PKT_TX_MACSEC;

    pkt->nb_segs        = 1;
    pkt->data_len       = pkt_size;
    pkt->pkt_len        = pkt_size;
    pkt->ol_flags       = ol_flags;
    pkt->vlan_tci       = ports[global_fs->tx_port].tx_vlan_id;
    pkt->vlan_tci_outer = ports[global_fs->tx_port].tx_vlan_id_outer;
    pkt->l2_len         = L2_LEN;
    pkt->l3_len         = L3_LEN;

    sender_pkts_burst[sender_current_burst_size_yog1] = pkt;
    sender_current_burst_size_yog1++;
    if (sender_current_burst_size_yog1 >= BURST_THRESHOLD) {
        sender_send_pkt();
        sender_current_burst_size_yog1 = 0;
        fprintf(fp,"send data flow_id = %d pkt_size = %d\n",flow_id,pkt_size);
    }
    // rte_spinlock_unlock(&send_lock_yog1);
    // if(DEBUG_LOCK_yog1) printf("[lock]construct_data release lock\n");
}



//标记为不能调用的函数可以看一下实现方法，实现内容与本项目有一定的出入
static void
start_warm_up_flow(void);//不能调用

static void
receiver_send_pkt(void);//不能调用

static void
init(void);

static void
read_config(void);

static inline void
print_ether_addr(const char *what, struct rte_ether_addr *eth_addr);

static inline void
add_receiver_active_flow(int flow_id);//不能调用

static inline void
remove_receiver_active_flow(int flow_id);//不能调用

static inline void
sort_receiver_active_flow_by_remaining_size(void);//不能调用

static inline int
find_next_unstart_flow_id(void);//不能调用

static inline void 
remove_newline(char *str);//不能调用

static void
update_remain_size(void);//不能调用

static inline void
add_sender_active_flow(int flow_id);//不能调用

static inline void
remove_sender_active_flow(int flow_id);//不能调用

// static inline int
// get_src_server_id(uint32_t flow_id, struct flow_info *flows);

static inline void
print_elapsed_time(void);

static void
print_fct(void);

static void show_unack_pktinfo(void);

static
int myLinkedListDeletebyInfo(int ack_len,uint32_t flow_id);



#ifdef NEEDARBITER

static void
Arbiter_record_new_flow_info(struct Parsing_info *Par_info, struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr);
// 当info pkt到达的时候，记录流信息

static void
Arbiter_record_new_flow_info(struct Parsing_info *Par_info, struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr)
{
    sender_flows[Par_info->flow_id].src_port = RTE_BE_TO_CPU_16(transport_recv_hdr->src_port);
    sender_flows[Par_info->flow_id].dst_port = RTE_BE_TO_CPU_16(transport_recv_hdr->dst_port);
    sender_flows[Par_info->flow_id].src_ip = rte_be_to_cpu_32(ipv4_hdr->src_addr);
    sender_flows[Par_info->flow_id].dst_ip = rte_be_to_cpu_32(ipv4_hdr->dst_addr);
    sender_flows[Par_info->flow_id].start_time = rte_rdtsc() / (double)hz;
    sender_flows[Par_info->flow_id].flow_size = Par_info->flow_size;
    sender_flows[Par_info->flow_id].rank_in_sender = Par_info->rank_in_sender;
    sender_flows[Par_info->flow_id].flow_finished = 0;
    sender_flows[Par_info->flow_id].remain_size = Par_info->flow_remain_size;
    sender_flows[Par_info->flow_id].receiver_recvlen = 0;
    sender_flows[Par_info->flow_id].receiver_finish_time = 0;
    sender_flows[Par_info->flow_id].unexcept_pkt_count = 0;
}

static void Syn_host_state(Parsing_info *Par_info, int src_or_dst);
// 第一步：读取流信息后同步端侧状态
static void Syn_host_state(Parsing_info *Par_info, int src_or_dst)
{
    if(!Par_info)
    {
        rte_panic("Par_info is NULL");
    }
    int src_host_id = -1;
    if (src_or_dst == 0)
    { // 如果是该流发送端发送的信息
        src_host_id = get_src_server_id(Par_info->flow_id,sender_flows);
        host_sending_flow_id_yog1[src_host_id] = Par_info->handing_flow_id;
        host_sender_ctl_type_yog1[src_host_id] = Par_info->host_ctl_type;
    }
    else if (src_or_dst == 1)
    {
        src_host_id = get_dst_server_id(Par_info->flow_id,sender_flows);
        host_receiving_flow_id_yog1[src_host_id] = Par_info->handing_flow_id;
        host_receiver_ctl_type_yog1[src_host_id] = Par_info->host_ctl_type;
    }
    else
    {
        RTE_LOG(WARNING, ACL, "syn state get wrong id!!!!!!!");
    }

    if (sender_flows[Par_info->flow_id].remain_size > Par_info->flow_remain_size)
    {
        sender_flows[Par_info->flow_id].remain_size = Par_info->flow_remain_size;
        // updateSet(arbiter_flows_set, Par_info->flow_id, sender_flows);
    }
}

static struct  rte_mbuf*
get_order_for_self(int flow_id, int where);

static struct  rte_mbuf*
get_order_for_self(int flow_id, int where)
{
    struct rte_ether_hdr *eth_hdr;
    struct rte_ipv4_hdr *ip_hdr;
    struct rte_tcp_hdr *transport_hdr;
    uint64_t ol_flags, tx_offloads;
    int dst_server_id;
    unsigned pkt_size = HDR_ONLY_SIZE;

    // struct rte_mempool *mbp = current_fwd_lcore()->mbp;
    struct rte_mbuf *pkt = rte_mbuf_raw_alloc(send_pool_yog1);
    if (!pkt)
    {
        if(SHOW_DEBUG_INFO_yog1) printf("flow_id = %d: allocation pkt error", flow_id);
        rte_panic("flow_id = %d: allocation pkt error", flow_id);
    }

    pkt->data_len = pkt_size;
    pkt->next = NULL;

    /* Initialize Ethernet header. */
    eth_hdr = rte_pktmbuf_mtod(pkt, struct ether_hdr *);
    dst_server_id = where;
    if (dst_server_id == -1)
    {
        if(SHOW_DEBUG_INFO_yog1) printf("server error: cannot find server id\n");
    }
    rte_ether_addr_copy(&eth_addr_array[dst_server_id], &eth_hdr->dst_addr);
    rte_ether_addr_copy(&eth_addr_array[this_server_id_yog1], &eth_hdr->src_addr);
    eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

    /* Initialize IP header. */
    ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
    memset(ip_hdr, 0, L3_LEN);
    ip_hdr->version_ihl = IP_VHL_DEF;
    ip_hdr->type_of_service = 0;
    ip_hdr->time_to_live = IP_DEFTTL;
    ip_hdr->next_proto_id = IPPROTO_TCP;
    ip_hdr->src_addr = rte_cpu_to_be_32(ip_addr_array[this_server_id_yog1]);
    ip_hdr->dst_addr = rte_cpu_to_be_32(ip_addr_array[where]);
    ip_hdr->total_length = RTE_CPU_TO_BE_16(pkt_size - L2_LEN);
    ip_hdr->hdr_checksum = ip_sum((unaligned_uint16_t *)ip_hdr, L3_LEN);

    /* Initialize transport header. */
    transport_hdr = (struct rte_tcp_hdr *)(ip_hdr + 1);
    transport_hdr->src_port = rte_cpu_to_be_16(sender_flows[flow_id].src_port);
    transport_hdr->dst_port = rte_cpu_to_be_16(sender_flows[flow_id].dst_port);
    transport_hdr->PKT_TYPE_8BITS = PT_ORDER; // 加入pkt_type
    transport_hdr->FLOW_ID_16BITS = rte_cpu_to_be_16((uint16_t)(flow_id & 0xffff));

    tx_offloads = ports[global_fs->tx_port].dev_conf.txmode.offloads;
    if (tx_offloads & DEV_TX_OFFLOAD_VLAN_INSERT)
        ol_flags = PKT_TX_VLAN_PKT;
    if (tx_offloads & DEV_TX_OFFLOAD_QINQ_INSERT)
        ol_flags |= PKT_TX_QINQ_PKT;
    if (tx_offloads & DEV_TX_OFFLOAD_MACSEC_INSERT)
        ol_flags |= PKT_TX_MACSEC;

    pkt->nb_segs = 1;
    pkt->data_len = pkt_size;
    pkt->pkt_len = pkt_size;
    pkt->ol_flags = ol_flags;
    pkt->vlan_tci = ports[global_fs->tx_port].tx_vlan_id;
    pkt->vlan_tci_outer = ports[global_fs->tx_port].tx_vlan_id_outer;
    pkt->l2_len = L2_LEN;
    pkt->l3_len = L3_LEN;

    return pkt;
}

int value[SERVERNUM][SERVERNUM]; // 记录每个妹子和每个男生的好感度
int ex_src[SERVERNUM];           // 每个妹子的期望值
int ex_dst[SERVERNUM];           // 每个男生的期望值
int vis_src[SERVERNUM];          // 记录每一轮匹配匹配过的女生
int vis_dst[SERVERNUM];          // 记录每一轮匹配匹配过的男生
int match[SERVERNUM];            // 记录每个男生匹配到的妹子 如果没有则为-1
int slack[SERVERNUM];            // 记录每个汉子如果能被妹子倾心最少还需要多少期望值
uint32_t use_flow[SERVERNUM][SERVERNUM];
const int INF_yog1 = 1<<27;
#define FLOW_MAX_SIZE_INBYTE 1000000

static int get_flow_value(int flow_id);

static int get_flow_value(int flow_id)
{
    return FLOW_MAX_SIZE_INBYTE / (sender_flows[flow_id].remain_size+1) + 1;
}

static void init_KM_value(void);

static void init_KM_value(void)
{
    for (int i = 1; i < SERVERNUM; i++)
    {
        for (int j = 1; j < SERVERNUM; j++)
        {
            value[i][j] = 0;
            use_flow[i][j] = 0;
        }
    }
    for (int src_id = 1; src_id < SERVERNUM; src_id++)
    {
        if (host_sender_ctl_type_yog1[src_id] == self_src)
            continue;

        Set *flows = host_send_flows[src_id];
        if (flows->head == NULL)
            continue;

        SetNode *cur_node = flows->head;

        while (cur_node != NULL)
        {
            int cur_flow_id = cur_node->flow_id;
            int dst_id = get_dst_server_id(cur_flow_id, sender_flows);
            if (host_receiver_ctl_type_yog1[dst_id] == self_dst)
            {
                cur_node = cur_node->next;
                continue;
            }

            int flow_value = get_flow_value(cur_flow_id);
            if (flow_value > value[src_id][dst_id])
            {
                value[src_id][dst_id] = flow_value;
                use_flow[src_id][dst_id] = cur_flow_id;
            }

            cur_node = cur_node->next;
        }
    }
}
static int dfs(int src);
static int dfs(int src)
{
    vis_src[src] = 1;

    for (int dst = 1; dst < SERVERNUM; ++dst)
    {

        if (vis_dst[dst])
            continue; // 每一轮匹配 每个男生只尝试一次

        int gap = ex_src[src] + ex_dst[dst] - value[src][dst];

        if (gap == 0)
        { // 如果符合要求
            vis_dst[dst] = 1;
            if (match[dst] == -1 || dfs(match[dst]))
            { // 找到一个没有匹配的男生 或者该男生的妹子可以找到其他人
                match[dst] = src;
                return 1;
            }
        }
        else
        {
            slack[dst] = min(slack[dst], gap); // slack 可以理解为该男生要得到女生的倾心 还需多少期望值 取最小值 备胎的样子【捂脸
        }
    }
    return 0;
}

static void KM(void);

static void KM(void)
{
    memset(match, -1, sizeof match);  // 初始每个男生都没有匹配的女生
    memset(ex_dst, 0, sizeof ex_dst); // 初始每个男生的期望值为0
    // 每个女生的初始期望值是与她相连的男生最大的好感度
    for (int i = 1; i < SERVERNUM; ++i)
    {
        ex_src[i] = value[i][0];
        for (int j = 2; j < SERVERNUM; ++j)
        {
            ex_src[i] = max(ex_src[i], value[i][j]);
        }
    }  

    // 尝试为每一个女生解决归宿问题
    for (int i = 1; i < SERVERNUM; ++i)
    {

        // std::fill(slack, slack + SERVERNUM, INF_yog1);    // 因为要取最小值 初始化为无穷大
        for (int x = 1; x < SERVERNUM; ++x)
            slack[x] = INF_yog1;
        while (1)
        {
            // 为每个女生解决归宿问题的方法是 ：如果找不到就降低期望值，直到找到为止

            // 记录每轮匹配中男生女生是否被尝试匹配过
            memset(vis_src, false, sizeof vis_src);
            memset(vis_dst, false, sizeof vis_dst);

            if (dfs(i))
                break; // 找到归宿 退出

            // 如果不能找到 就降低期望值
            // 最小可降低的期望值
            int d = INF_yog1;
            for (int j = 1; j < SERVERNUM; ++j)
                if (!vis_dst[j])
                    d = min(d, slack[j]);

            for (int j = 1; j < SERVERNUM; ++j)
            {
                // 所有访问过的女生降低期望值
                if (vis_src[j])
                    ex_src[j] -= d;

                // 所有访问过的男生增加期望值
                if (vis_dst[j])
                    ex_dst[j] += d;
                // 没有访问过的dst 因为src们的期望值降低，距离得到女生倾心又进了一步！
                else
                    slack[j] -= d;
            }
        }
    }
}
static void
construct_order(int flow_id, int where);

static void
construct_order(int flow_id, int where)
{
    struct rte_ether_hdr *eth_hdr;
    struct rte_ipv4_hdr *ip_hdr;
    struct rte_tcp_hdr *transport_hdr;
    uint64_t ol_flags, tx_offloads;
    int dst_server_id;
    unsigned pkt_size = HDR_ONLY_SIZE;

    // struct rte_mempool *mbp = current_fwd_lcore()->mbp;
    struct rte_mbuf *pkt = rte_mbuf_raw_alloc(send_pool_yog1);
    if (!pkt)
    {
        if(SHOW_DEBUG_INFO_yog1) printf("flow_id = %d: allocation pkt error", flow_id);
        rte_panic("flow_id = %d: allocation pkt error", flow_id);
        
    }

    pkt->data_len = pkt_size;
    pkt->next = NULL;

    /* Initialize Ethernet header. */
    eth_hdr = rte_pktmbuf_mtod(pkt, struct ether_hdr *);
    dst_server_id = where;
    if (dst_server_id == -1)
    {
        if(SHOW_DEBUG_INFO_yog1) printf("server error: cannot find server id\n");
    }
    rte_ether_addr_copy(&eth_addr_array[dst_server_id], &eth_hdr->dst_addr);
    rte_ether_addr_copy(&eth_addr_array[this_server_id_yog1], &eth_hdr->src_addr);
    eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

    /* Initialize IP header. */
    ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
    memset(ip_hdr, 0, L3_LEN);
    ip_hdr->version_ihl = IP_VHL_DEF;
    ip_hdr->type_of_service = (uint8_t)(0x05 << 2);
    ip_hdr->time_to_live = IP_DEFTTL;
    ip_hdr->next_proto_id = IPPROTO_TCP;
    ip_hdr->src_addr = rte_cpu_to_be_32(ip_addr_array[this_server_id_yog1]);
    ip_hdr->dst_addr = rte_cpu_to_be_32(ip_addr_array[where]);
    ip_hdr->total_length = RTE_CPU_TO_BE_16(pkt_size - L2_LEN);
    ip_hdr->hdr_checksum = ip_sum((unaligned_uint16_t *)ip_hdr, L3_LEN);

    /* Initialize transport header. */
    transport_hdr = (struct tcp_hdr *)(ip_hdr + 1);
    transport_hdr->src_port = rte_cpu_to_be_16(sender_flows[flow_id].src_port);
    transport_hdr->dst_port = rte_cpu_to_be_16(sender_flows[flow_id].dst_port);
    transport_hdr->PKT_TYPE_8BITS = PT_ORDER; // 加入pkt_type
    transport_hdr->recv_ack              = rte_cpu_to_be_32(sender_flows[flow_id].rank_in_sender);
    transport_hdr->FLOW_REMAIN_SIZE          = rte_cpu_to_be_32(sender_flows[flow_id].remain_size);
    transport_hdr->FLOW_ID_16BITS = rte_cpu_to_be_16((uint16_t)(flow_id & 0xffff));

    tx_offloads = ports[global_fs->tx_port].dev_conf.txmode.offloads;
    if (tx_offloads & DEV_TX_OFFLOAD_VLAN_INSERT)
        ol_flags = PKT_TX_VLAN_PKT;
    if (tx_offloads & DEV_TX_OFFLOAD_QINQ_INSERT)
        ol_flags |= PKT_TX_QINQ_PKT;
    if (tx_offloads & DEV_TX_OFFLOAD_MACSEC_INSERT)
        ol_flags |= PKT_TX_MACSEC;

    pkt->nb_segs = 1;
    pkt->data_len = pkt_size;
    pkt->pkt_len = pkt_size;
    pkt->ol_flags = ol_flags;
    pkt->vlan_tci = ports[global_fs->tx_port].tx_vlan_id;
    pkt->vlan_tci_outer = ports[global_fs->tx_port].tx_vlan_id_outer;
    pkt->l2_len = L2_LEN;
    pkt->l3_len = L3_LEN;

    // if(DEBUG_LOCK_yog1) printf("[lock]construct_order try to get lock\n");
    // rte_spinlock_lock(&send_lock_yog1);
    // if(DEBUG_LOCK_yog1) printf("[lock]construct_order got lock\n");
    sender_pkts_burst[sender_current_burst_size_yog1] = pkt;
    sender_current_burst_size_yog1++;
    if (sender_current_burst_size_yog1 >= BURST_THRESHOLD)
    {
        sender_send_pkt();
        sender_current_burst_size_yog1 = 0;
    }
    // rte_spinlock_unlock(&send_lock_yog1);
    // if(DEBUG_LOCK_yog1) printf("[lock]construct_order release lock\n");
}

static void Find_order_Flow(void);

static void Find_order_Flow(void)
{
    for (int i = 1; i < SERVERNUM; i++)
    {
        if (match[i] <= 0 || match[i] >= SERVERNUM)
            continue;
        if (use_flow[match[i]][i] > 0)
        {
            uint32_t fill_flow_id = use_flow[match[i]][i];
            if (match[i] != get_src_server_id(fill_flow_id, sender_flows))
                RTE_LOG(WARNING, ACL, "error flow in error place");
            // if(host_sending_flow_id_yog1[f->src->id]==f->id && host_receiving_flow_id_yog1[f->dst->id]==f->id)
            //     continue;
            int src_id = get_src_server_id(fill_flow_id, sender_flows);
            int dst_id = get_dst_server_id(fill_flow_id, sender_flows);
            int src_sending = host_sending_flow_id_yog1[src_id];
            int dst_recving = host_receiving_flow_id_yog1[dst_id];
            if (src_sending == fill_flow_id && dst_recving == fill_flow_id)
                continue;
            if(this_server_id_yog1!=src_id && this_server_id_yog1!=dst_id)
            {
                construct_order(fill_flow_id, src_id);
                construct_order(fill_flow_id, dst_id);
            }
            else{
                if(this_server_id_yog1==src_id){
                    construct_order(fill_flow_id, dst_id);
                }
                else{
                    construct_order(fill_flow_id, src_id);
                }
                    struct rte_mbuf *mb = get_order_for_self(fill_flow_id,this_server_id_yog1);
                    struct rte_ipv4_hdr *ipv4_hdr = rte_pktmbuf_mtod_offset(mb, struct rte_ipv4_hdr *, L2_LEN);
                    struct rte_tcp_hdr *transport_recv_hdr  = rte_pktmbuf_mtod_offset(mb, struct rte_tcp_hdr *, L2_LEN + L3_LEN);
                    recv_order(transport_recv_hdr, ipv4_hdr);
            }
        }
    }
}

static void Try_fill_bandwidth(void);
// 第三步，尝试根据当前状态进行带宽填充
static void Try_fill_bandwidth(void)
{
    init_KM_value();
    // if(SHOW_DEBUG_INFO_yog1) printf("enter KM\n");
    KM();
    // if(SHOW_DEBUG_INFO_yog1) printf("out KM\n");
    Find_order_Flow();
    // if(SHOW_DEBUG_INFO_yog1) printf("out Find_order_Flow\n");
}

#endif

static void
show_flow_info(uint32_t flow_id);

static void
show_flow_info(uint32_t flow_id)
{
    // 根据是发送端还是接收端，打印流量信息
    if (get_dst_server_id(flow_id, sender_flows) == this_server_id_yog1)
    {
        if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "flow_id:%d, flow_size:%d, flow_remain:%d ,revlen:%d ,rank:%d\n",
                flow_id, receiver_flows[flow_id].flow_size, receiver_flows[flow_id].remain_size, receiver_flows[flow_id].receiver_recvlen, receiver_flows[flow_id].rank_in_sender);
    }
    else if (get_src_server_id(flow_id, sender_flows) == this_server_id_yog1)
    {
        if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "flow_id:%d, flow_size:%d, flow_remain:%d ,can_send_size:%d , wait count: %d ,rank:%d \n",
                flow_id, sender_flows[flow_id].flow_size, sender_flows[flow_id].remain_size, sender_flows[flow_id].sender_can_send_size, sender_flows[flow_id].sender_wait_count, sender_flows[flow_id].rank_in_sender);
    }
    else
    {
        // if(SHOW_DEBUG_INFO_yog1) printf("error flow id:%d in server %d\n", flow_id, this_server_id_yog1);
        if(SHOW_DEBUG_INFO_yog1) printf("Flow not belong to this server %d, flow id %d.\n", this_server_id_yog1, flow_id);
    }
}

static void
show_host_info(void);


static void
show_host_info(void){
    if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"this_server_id_yog1:%d, sender_ctl_type_yog1:%d, recv_ctl_type %d , sending id %d , recving id %d\n"
       ,this_server_id_yog1
       ,sender_ctl_type_yog1
        , receiver_ctl_type_yog1
        ,sending_flow_id_yog1
        ,receiving_flow_id_yog1
    );
        SetNode *temp = receiver_firmflow_arr->head;
        if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"receiver firm flow list= ");
        while (temp != NULL)
        {
            if(SAVE_DEBUG_INFO_yog1) fprintf(fp," id:  %d  flow size: %d remain_size = %d", temp->flow_id, receiver_flows[temp->flow_id].flow_size,receiver_flows[temp->flow_id].remain_size);
            temp = temp->next;
        }
        if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"\n");

        temp = sender_largeflow_arr->head;
        if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"sender large flows = ");
        while (temp != NULL)
        {
            if(SAVE_DEBUG_INFO_yog1) fprintf(fp," id:  %d  flow size: %d remain_size = %d", temp->flow_id, sender_flows[temp->flow_id].flow_size,sender_flows[temp->flow_id].remain_size);
            temp = temp->next;
        }
        if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"\n");
        temp = sender_smallflow_arr->head;
        if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"send small flow = ");
        while (temp != NULL)
        {
            if(SAVE_DEBUG_INFO_yog1) fprintf(fp," id:  %d  can_Send: %d ", temp->flow_id, sender_flows[temp->flow_id].sender_can_send_size);
            temp = temp->next;
        }
        if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"\n");
}

static void
LOG_DEBUG(uint32_t log_le, const char *file, const int line, uint32_t flow_id, const char *str)
{
    if (log_le != log_level)
        return;
    if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "\n-------------LOG_DEBUG begin-----------\n");
    if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "%s [trigger by F:%s | L:%d]", str, file, line);
    if (flow_id < 1)
        return;
    if (show_host)
        show_host_info();
    if (show_flow)
        show_flow_info(flow_id);
    if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "-------------- LOG_DEBUG end------------- \n");
}

static void
send_ctl_pkt(int pkt_type, int flow_id, int arbiter)
{
    int dst_id;
    if (get_src_server_id(flow_id, sender_flows) == this_server_id_yog1) // 如果本端点是发送端
        dst_id = get_dst_server_id(flow_id, sender_flows);
    else
        dst_id = get_src_server_id(flow_id, sender_flows); // 如果是接收端
    construct_ctl_pkt(pkt_type, flow_id, dst_id);

    switch (pkt_type)
    {
    case PT_INF_yog1O:
    case PT_GRANT:
    case PT_CHANGE_RANK_1:
    case PT_CHANGE_RANK_2:
    case PT_FLOW_FINISH:
        myLinkedListAddAtTail_ctl(flow_id,pkt_type,rte_rdtsc() / (double)hz);
        break;
    
    default:
        break;
    }

    char *temp_string = malloc(sizeof(char) * 128);
    sprintf(temp_string, "[send_ctl_pkt]host id = %d beacuse flowid = %d send ctl pkt %x to %d\n", this_server_id_yog1, flow_id, pkt_type, dst_id);
    LOG_DEBUG(1, __FILE__, __LINE__, flow_id, temp_string);

    if (arbiter == 1 && this_server_id_yog1 != arbiter_server_id_yog1)
    { // 如果该信息要发送给集中控制器
        if(SHOW_DEBUG_INFO_yog1) printf("Will send ctl to arbiter %d\n", arbiter_server_id_yog1);
        construct_ctl_pkt(pkt_type | 0x40, flow_id, arbiter_server_id_yog1);
    }
}

// static void
// send_ctl_pkt(int pkt_type,int flow_id,int arbiter)
// {  

// }


static void
construct_ctl_pkt(int pkt_type,int flow_id,int where)//构建数据包并加入队列
{
    struct   rte_ether_hdr *eth_hdr;
    struct   rte_ipv4_hdr *ip_hdr;
    struct   rte_tcp_hdr *transport_hdr;
    uint64_t ol_flags, tx_offloads;
    int      dst_server_id;
    unsigned pkt_size = HDR_ONLY_SIZE;

    // if(DEBUG_LOCK_yog1) printf("[lock]construct_ctl_pkt try to get lock\n");
    // rte_spinlock_lock(&send_lock_yog1);
    // if(DEBUG_LOCK_yog1) printf("[lock]construct_ctl_pkt got lock\n");
    
    // struct rte_mempool *mbp = current_fwd_lcore()->mbp;
    struct rte_mbuf *pkt = rte_mbuf_raw_alloc(send_pool_yog1);
    if (!pkt) {
        if(SHOW_DEBUG_INFO_yog1) printf("flow_id = %d: allocation pkt error", flow_id);
        rte_panic("flow_id = %d: allocation pkt error", flow_id);
    }

    pkt->data_len = pkt_size;
    pkt->next = NULL;

    /* Initialize Ethernet header. */
    eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
    dst_server_id = where;
    if (dst_server_id == -1) {
        if(SHOW_DEBUG_INFO_yog1) printf("server error: cannot find server id\n");
    }
    rte_ether_addr_copy(&eth_addr_array[dst_server_id], &eth_hdr->dst_addr);
    rte_ether_addr_copy(&eth_addr_array[this_server_id_yog1], &eth_hdr->src_addr);
    eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

    /* Initialize IP header. */
    ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
    memset(ip_hdr, 0, L3_LEN);
    ip_hdr->version_ihl     = IP_VHL_DEF;
    ip_hdr->type_of_service = (uint8_t)(0x05 << 2);
    ip_hdr->time_to_live    = IP_DEFTTL;
    ip_hdr->next_proto_id   = IPPROTO_TCP;
    // ip_hdr->RANK_IN_SENDER  = rte_cpu_to_be_16(sender_flows[flow_id].rank_in_sender);
    ip_hdr->src_addr        = rte_cpu_to_be_32(ip_addr_array[this_server_id_yog1]);
    ip_hdr->dst_addr        = rte_cpu_to_be_32(ip_addr_array[where]);
    // printf("目的地址 = %d, 源地址 = %d\n",ip_hdr->dst_addr,ip_hdr->src_addr);
    ip_hdr->total_length    = RTE_CPU_TO_BE_16(pkt_size - L2_LEN);
   

    /* Initialize transport header. */
    transport_hdr = (struct rte_tcp_hdr *)(ip_hdr + 1);
    transport_hdr->src_port              = rte_cpu_to_be_16(sender_flows[flow_id].src_port);
    transport_hdr->dst_port              = rte_cpu_to_be_16(sender_flows[flow_id].dst_port);
    if(pkt_type==PT_ACK)
        transport_hdr->recv_ack              = rte_cpu_to_be_32(receiver_flows[flow_id].receiver_recvlen);
    else    
        transport_hdr->recv_ack              = rte_cpu_to_be_32(sender_flows[flow_id].rank_in_sender);
    transport_hdr->PKT_TYPE_8BITS        = pkt_type;//加入pkt_type
    transport_hdr->FLOW_ID_16BITS        = rte_cpu_to_be_16((uint16_t)(flow_id & 0xffff));
    transport_hdr->FLOW_SIZE_LOW_16BITS  = rte_cpu_to_be_16((uint16_t)(sender_flows[flow_id].flow_size & 0xffff));
    transport_hdr->FLOW_SIZE_HIGH_16BITS = (uint16_t)((sender_flows[flow_id].flow_size >> 16) & 0xffff);
    
    // 加入额外控制信息，后续需要验证一下正确性
    if(get_src_server_id(flow_id,sender_flows)==this_server_id_yog1){//是发送端
        if(sending_flow_id_yog1 > 0)
            ip_hdr->HANDLING_FLOW_ID_16BITS      = rte_cpu_to_be_16((uint16_t)(sending_flow_id_yog1 & 0xffff));
        else    
            ip_hdr->HANDLING_FLOW_ID_16BITS      = rte_cpu_to_be_16((uint16_t)(0 & 0xffff));
        transport_hdr->HOST_CTL_TYPE_8BIT        = sender_ctl_type_yog1;
        transport_hdr->FLOW_REMAIN_SIZE          = rte_cpu_to_be_32(sender_flows[flow_id].remain_size);
    }
    else{
        if(receiving_flow_id_yog1 > 0)
            ip_hdr->HANDLING_FLOW_ID_16BITS      = rte_cpu_to_be_16((uint16_t)(receiving_flow_id_yog1 & 0xffff));
        else    
            ip_hdr->HANDLING_FLOW_ID_16BITS      = rte_cpu_to_be_16((uint16_t)(0 & 0xffff));
        transport_hdr->HOST_CTL_TYPE_8BIT        = receiver_ctl_type_yog1;
        transport_hdr->FLOW_REMAIN_SIZE          = rte_cpu_to_be_32(receiver_flows[flow_id].remain_size);
    }
    ip_hdr->hdr_checksum = ip_sum((unaligned_uint16_t *)ip_hdr, L3_LEN);

    tx_offloads = ports[global_fs->tx_port].dev_conf.txmode.offloads;
    if (tx_offloads & DEV_TX_OFFLOAD_VLAN_INSERT)
        ol_flags = PKT_TX_VLAN_PKT;
    if (tx_offloads & DEV_TX_OFFLOAD_QINQ_INSERT)
        ol_flags |= PKT_TX_QINQ_PKT;
    if (tx_offloads & DEV_TX_OFFLOAD_MACSEC_INSERT)
        ol_flags |= PKT_TX_MACSEC;

    pkt->nb_segs        = 1;
    pkt->data_len       = pkt_size;
    pkt->pkt_len        = pkt_size;
    pkt->ol_flags       = ol_flags;
    pkt->vlan_tci       = ports[global_fs->tx_port].tx_vlan_id;
    pkt->vlan_tci_outer = ports[global_fs->tx_port].tx_vlan_id_outer;
    pkt->l2_len         = L2_LEN;
    pkt->l3_len         = L3_LEN;

    if(SHOW_DEBUG_INFO_yog1) printf("will send ctl_pkt %x to %d\n", pkt_type, where);

    
    sender_pkts_burst[sender_current_burst_size_yog1] = pkt;
    sender_current_burst_size_yog1++;
    if (sender_current_burst_size_yog1 >= BURST_THRESHOLD) {
        sender_send_pkt();
        sender_current_burst_size_yog1 = 0;
    }
    // rte_spinlock_unlock(&send_lock_yog1);
    // if(DEBUG_LOCK_yog1) printf("[lock]construct_ctl_pkt release lock\n");
}


static MyLinkedList* myLinkedListCreate(void)
{
    MyLinkedList* myLink = (MyLinkedList*)malloc(sizeof(MyLinkedList));
    myLink->count = 0;
    myLink->head = NULL;
    myLink->rear = NULL;
    return myLink;
}


static void myLinkedListAddAtTail( uint32_t flow_id,int end_pos ,int pkt_size, double send_time )
{
    struct pktNode* newNode = (struct pktNode*)malloc(sizeof(struct pktNode));
    newNode->flow_id = flow_id;
    newNode->send_time = send_time;
    newNode->end_pos = end_pos;
    newNode->pkt_size = pkt_size;
    newNode->prev = NULL;
    newNode->next = NULL;
    if (Unackpkts_list->head == NULL)
    {
        Unackpkts_list->head = newNode;
        Unackpkts_list->rear = newNode;
        Unackpkts_list->count++;
    }
    else
    {
        Unackpkts_list->rear->next = newNode;
        newNode->prev = Unackpkts_list->rear;

        Unackpkts_list->rear = newNode;
        Unackpkts_list->count++;
    }
}

static void myLinkedListAddAtTail_ctl(uint32_t flow_id,int pkt_type, double send_time )
{
    struct pktNode* newNode = (struct pktNode*)malloc(sizeof(struct pktNode));
    newNode->flow_id = flow_id;
    newNode->send_time = send_time;
    newNode->pkt_type = pkt_type;
    newNode->ifvalid = 1;
    newNode->prev = NULL;
    newNode->next = NULL;
    if (ctl_pkt_list->head == NULL)
    {
        ctl_pkt_list->head = newNode;
        ctl_pkt_list->rear = newNode;
        ctl_pkt_list->count++;
    }
    else
    {
        ctl_pkt_list->rear->next = newNode;
        newNode->prev = ctl_pkt_list->rear;

        ctl_pkt_list->rear = newNode;
        ctl_pkt_list->count++;
    }
}

static void myLinkedListDeleteHead(void)
{
    if (Unackpkts_list->count < 1)
    {
        return;
    }
    struct pktNode* nodeIt = Unackpkts_list->head;

    Unackpkts_list->head = Unackpkts_list->head->next;
    if(Unackpkts_list->head != NULL)
        Unackpkts_list->head->prev = NULL;
    else
        Unackpkts_list->rear = NULL;
    Unackpkts_list->count--;
    free(nodeIt);
}

static void myLinkedListDeleteHead_ctl(void)
{
    if (ctl_pkt_list->count < 1)
    {
        return;
    }
    struct pktNode* nodeIt = ctl_pkt_list->head;

    ctl_pkt_list->head = ctl_pkt_list->head->next;
    if(ctl_pkt_list->head != NULL)
        ctl_pkt_list->head->prev = NULL;
    else
        ctl_pkt_list->rear = NULL;
    ctl_pkt_list->count--;
    free(nodeIt);
}

static int myLinkedListDeletebyInfo(int ack_len,uint32_t flow_id)
{
    struct pktNode* nodeIt = Unackpkts_list->head;
    int find = 0;
    while (nodeIt != NULL)
    {

        if(nodeIt->flow_id==flow_id && nodeIt->end_pos<=ack_len)
        {
            find = 1;
            break;
        }
        nodeIt = nodeIt->next;
    }
    if(find==0)
        return 0;//没有找到
    sender_flows[flow_id].sender_unack_size-=nodeIt->pkt_size;

    
    if (nodeIt == Unackpkts_list->head)
    {
        Unackpkts_list->head = Unackpkts_list->head->next;
        if(Unackpkts_list->head != NULL)
            Unackpkts_list->head->prev = NULL;
    }
    else if (nodeIt == Unackpkts_list->rear)
    {
        Unackpkts_list->rear = Unackpkts_list->rear->prev;
        if (Unackpkts_list->rear != NULL)
            Unackpkts_list->rear->next = NULL;
    }
    else
    {
        nodeIt->prev->next = nodeIt->next;
        nodeIt->next->prev = nodeIt->prev;
    }
    Unackpkts_list->count--;

    if (Unackpkts_list->count == 0)
    {
        Unackpkts_list->rear = NULL;
        Unackpkts_list->head = NULL;
    }
    free(nodeIt);
    return 1;//找到了
}

static int myLinkedListDeletebyInfo_ctl(int pkt_type,uint32_t flow_id)
{
    struct pktNode* nodeIt = ctl_pkt_list->head;
    int find = 0;
    while (nodeIt != NULL)
    {

        if(nodeIt->flow_id==flow_id && nodeIt->pkt_type == pkt_type && nodeIt->ifvalid==1)
        {
            find = 1;
            nodeIt->ifvalid = 0;
            break;
        }
        nodeIt = nodeIt->next;
    }
    if(find==0)
        return 0;//没有找到

    
    // if (nodeIt == ctl_pkt_list->head)
    // {
    //     ctl_pkt_list->head = ctl_pkt_list->head->next;
    //     if(ctl_pkt_list->head != NULL)
    //         ctl_pkt_list->head->prev = NULL;
    // }
    // else if (nodeIt == ctl_pkt_list->rear)
    // {
    //     ctl_pkt_list->rear = ctl_pkt_list->rear->prev;
    //     if (ctl_pkt_list->rear != NULL)
    //         ctl_pkt_list->rear->next = NULL;
    // }
    // else
    // {
    //     nodeIt->prev->next = nodeIt->next;
    //     nodeIt->next->prev = nodeIt->prev;
    // }
    // ctl_pkt_list->count--;

    // if (ctl_pkt_list->count == 0)
    // {
    //     ctl_pkt_list->rear = NULL;
    //     ctl_pkt_list->head = NULL;
    // }
    // free(nodeIt);
    return 1;//找到了
}

static void show_unack_pktinfo(void){
    struct pktNode* nodeIt =  Unackpkts_list->head;
    int count = 0;
    while (nodeIt != NULL)
    {
        if(SHOW_DEBUG_INFO_yog1) printf("PKT %d flowid=%d endpos=%d pktsize=%d send_time=%lf\n",count,nodeIt->flow_id,nodeIt->end_pos,nodeIt->pkt_size,nodeIt->send_time);
        count++;
        nodeIt = nodeIt->next;
    }
}

static void find_timeout_pkt(void){
    struct pktNode* nodeIt = Unackpkts_list->head;
    while (nodeIt != NULL)
    {
        double now = rte_rdtsc() / (double)hz;
        if(now-nodeIt->send_time>TIME_OUT_TIME){//超时了
            sender_flows[nodeIt->flow_id].sender_unack_size -= nodeIt->pkt_size;//归还这些数据包，使之可以继续发送
            sender_flows[nodeIt->flow_id].sender_can_send_size += nodeIt->pkt_size;
            myLinkedListDeleteHead();
        }
        else//如果当前没有超时，后面的也不会超时的
            break;
        nodeIt = Unackpkts_list->head;
    }
}

static void find_timeout_ctl_pkt(void)
{
    struct pktNode *nodeIt = ctl_pkt_list->head;
    while (nodeIt != NULL)
    {
        double now = rte_rdtsc() / (double)hz;
        if (now - nodeIt->send_time > 1.5 * RTT)
        { // 超时了
            if (nodeIt->ifvalid == 0                                                        ||
                (nodeIt->pkt_type == PT_GRANT && receiving_flow_id_yog1 != nodeIt->flow_id) ||
                (nodeIt->pkt_type == PT_CHANGE_RANK_1 && sender_flows[nodeIt->flow_id].rank_in_sender != 1) ||
                (nodeIt->pkt_type == PT_CHANGE_RANK_2 && sender_flows[nodeIt->flow_id].rank_in_sender != 2) ||
                sender_flows[nodeIt->flow_id].flow_finished==1                                              ||
                receiver_flows[nodeIt->flow_id].flow_finished==1
                )
            {
                    myLinkedListDeleteHead_ctl();
            }
            else
            {
                    send_ctl_pkt(nodeIt->pkt_type, nodeIt->flow_id, 0);
                    if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "[Warn]CTL_PKT retansmit!\n");
                    myLinkedListDeleteHead_ctl();
            }
        }
        else // 如果当前没有超时，后面的也不会超时的
            break;
        nodeIt = ctl_pkt_list->head;
    }
}

static inline uint16_t
ip_sum(const unaligned_uint16_t *hdr, int hdr_len)
{
    uint32_t sum = 0;

    while (hdr_len > 1) {
        sum += *hdr++;
        if (sum & 0x80000000)
            sum = (sum & 0xFFFF) + (sum >> 16);
        hdr_len -= 2;
    }

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return ~sum;
}

static void 
remove_newline(char *str)
{
    for (uint32_t i = 0; i < strlen(str); i++) {
        if (str[i] == '\r' || str[i] == '\n')
            str[i] = '\0';
    }
}

static inline void
print_ether_addr(const char *what, struct rte_ether_addr *eth_addr)
{
    char buf[RTE_ETHER_ADDR_FMT_SIZE];
    rte_ether_format_addr(buf, RTE_ETHER_ADDR_FMT_SIZE, eth_addr);
    printf("%s%s\n", what, buf);
}



/* Map flow src ip to server id */
static inline int
get_src_server_id(uint32_t flow_id, struct flow_info *flows) 
{
    for (int server_index =0; server_index < SERVERNUM; server_index++) {
        if (flows[flow_id].src_ip == ip_addr_array[server_index]) {
            return server_index; 
        }
    }
    return -1;
}

/* Map flow dst ip to server id */
static inline int
get_dst_server_id(uint32_t flow_id, struct flow_info *flows) 
{

    for (int server_index =0; server_index < SERVERNUM; server_index++) {
        // if(SHOW_DEBUG_INFO_yog1) printf("index = %d is %d\n",server_index,ip_addr_array[server_index]);
        if (flows[flow_id].dst_ip == ip_addr_array[server_index]) {
            return server_index; 
        }
    }
    return -1;
}





static inline int
find_next_unstart_flow_id(void)
{
    int i;
    for (i=sender_next_unstart_flow_id_yog1+1; i<total_flow_num_yog1; i++) {
        if (get_src_server_id(i, sender_flows) == this_server_id_yog1)
            return i;
    }
    return i;
}

/* Update and print global elapsed time */
static inline void
print_elapsed_time(void)
{
    elapsed_cycle = rte_rdtsc() - start_cycle;
    printf("Time: %lf", elapsed_cycle/(double)hz);
}

// static void
// print_fct(void)
// {
//     if(SHOW_DEBUG_INFO_yog1) printf("Summary:\ntotal_flow_num_yog1 = %d (including %d warm up flows)\n"
//         "sender_total_flow_num_yog1_1 = %d, sender_finished_flow_num_yog1 = %d\n"
//         "receiver_total_flow_num_yog1_1 = %d, receiver_finished_flow_num_yog1 = %d\n"
//         "max_receiver_active_flow_num_yog1_1 = %d\n",
//         total_flow_num_yog1, SERVERNUM-1, sender_total_flow_num_yog1_1, sender_finished_flow_num_yog1,
//         receiver_total_flow_num_yog1_1, receiver_finished_flow_num_yog1, max_receiver_active_flow_num_yog1_1);

//     /* print_option: 0 - fct only
//                      1 - unfinished flows as well */
//     int print_option = 1;
//     double all_fct = 0;
//     if(SHOW_DEBUG_INFO_yog1) printf("Sender FCT:\n");
//     for (int i=0; i<total_flow_num_yog1; i++) {
//         if (sender_flows[i].fct_printed == 0 &&
//             sender_flows[i].src_ip == ip_addr_array[this_server_id_yog1]) {
//             if (sender_flows[i].flow_finished == 1){
//                 if(SHOW_DEBUG_INFO_yog1) printf("flow %d - fct = %lf, start_time = %lf\n", i, 
//                     sender_flows[i].finish_time - sender_flows[i].first_grant_access_time,
//                     sender_flows[i].first_grant_access_time);
//                 if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"flow %d - fct = %lf, start_time = %lf\n", i, 
//                     sender_flows[i].finish_time - sender_flows[i].first_grant_access_time,
//                     sender_flows[i].first_grant_access_time);
//             }
//             else if (print_option == 1)
//                 if(SHOW_DEBUG_INFO_yog1) printf("flow %d - flow_size = %u, remain_size = %u,unacklen = %d, acklen = %d\n", i, sender_flows[i].flow_size, 
//                     sender_flows[i].remain_size, sender_flows[i].send_unack_size, 
//                     sender_flows[i].send_acklen);
//             sender_flows[i].fct_printed = 1;
//             if(sender_flows[i].flow_finished == 1)
//                 all_fct+=sender_flows[i].finish_time - sender_flows[i].first_grant_access_time;
//         }
//     }
//     if(SHOW_DEBUG_INFO_yog1) printf("finish %d flow avgfct = %lf\n",sender_finished_flow_num_yog1,all_fct/sender_finished_flow_num_yog1);
//     if(SHOW_DEBUG_INFO_yog1) printf("Receiver FCT:\n");
//     for (int i=0; i<total_flow_num_yog1; i++) {
//         if (receiver_flows[i].fct_printed == 0 &&
//             receiver_flows[i].src_ip == ip_addr_array[this_server_id_yog1]) {
//             if (receiver_flows[i].flow_finished == 1)
//                 if(SHOW_DEBUG_INFO_yog1) printf("flow %d - fct = %lf, start_time = %lf, first_grant_send_time = %lf, "
//                     "resend_request_counter = %d\n", i, 
//                     receiver_flows[i].finish_time - receiver_flows[i].start_time,
//                     receiver_flows[i].start_time, receiver_flows[i].first_grant_access_time,
//                     receiver_flows[i].resend_request_counter);
//             else if (print_option == 1)
//                 if(SHOW_DEBUG_INFO_yog1) printf("flow %d - flow_size = %u, remain_size = %u, start_time = %lf, "
//                     "first_grant_send_time = %lf, resend_request_counter = %d\n", 
//                     i, receiver_flows[i].flow_size, receiver_flows[i].remain_size,
//                     receiver_flows[i].start_time, receiver_flows[i].first_grant_access_time,
//                     receiver_flows[i].resend_request_counter);
//             receiver_flows[i].fct_printed = 1;
//         }
//     }
// }

/* Read basic info of server id, mac and ip */
static void
read_config(void)
{
    FILE *fd = NULL;
    char line[256] = {0};
    int  server_id;
    uint32_t src_ip_segment1 = 0, src_ip_segment2 = 0, src_ip_segment3 = 0, src_ip_segment4 = 0;

    /* Read ethernet address info */
    server_id = 0;
    fd = fopen(ethaddr_filename, "r");
    if (!fd)
        if(SHOW_DEBUG_INFO_yog1) printf("%s: no such file\n", ethaddr_filename);
    while (fgets(line, sizeof(line), fd) != NULL) {
        remove_newline(line);
        rte_ether_unformat_addr(line, &eth_addr_array[server_id]);
        // sscanf(line, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &eth_addr_array[server_id].addr_bytes[0], 
        //     &eth_addr_array[server_id].addr_bytes[1], &eth_addr_array[server_id].addr_bytes[2],
        //     &eth_addr_array[server_id].addr_bytes[3], &eth_addr_array[server_id].addr_bytes[4], 
        //     &eth_addr_array[server_id].addr_bytes[5]);
        // if (verbose_yog1 > 0) {
           printf("Server id = %d   ", server_id);
           print_ether_addr("mac = ", &eth_addr_array[server_id]);
        // }
        server_id++;
    }

    for(int i=0; i<=8; i++)
    {
        printf("eth_addr_array[%d] = %x:%x:%x:%x:%x:%x\n", i, eth_addr_array[i].addr_bytes[0], eth_addr_array[i].addr_bytes[1], eth_addr_array[i].addr_bytes[2], eth_addr_array[i].addr_bytes[3], eth_addr_array[i].addr_bytes[4], eth_addr_array[i].addr_bytes[5]);
    }
    fclose(fd);

    /* Read ip address info */
    server_id = 0;
    fd = fopen(ipaddr_filename, "r");
    if (!fd)
        if(SHOW_DEBUG_INFO_yog1) printf("%s: no such file\n", ipaddr_filename);
    while (fgets(line, sizeof(line), fd) != NULL) {
        remove_newline(line);
        sscanf(line, "%u %u %u %u", &src_ip_segment1, &src_ip_segment2, &src_ip_segment3, &src_ip_segment4);
        // ip_addr_array[server_id] = RTE_IPV4(src_ip_segment1, src_ip_segment2, src_ip_segment3, src_ip_segment4);
        ip_addr_array[server_id] = IPv4(src_ip_segment1, src_ip_segment2, src_ip_segment3, src_ip_segment4);
        if (this_server_id_yog1 == server_id) {
            printf("Server id = %d   ", server_id);
            printf("ip = %u.%u.%u.%u (%u)\n", src_ip_segment1, src_ip_segment2, 
                src_ip_segment3, src_ip_segment4, ip_addr_array[server_id]);
        }
        server_id++;
    }
    fclose(fd);
}

static void sigintHandler(int signum) {
    printf("Received SIGINT signal. Exiting...\n");
    
    print_fct();
    
    // 退出程序
    exit(0);
}

/* Init flow info */
static void
init(void)
{
    // rte_spinlock_init(&send_lock_yog1);
    signal(SIGINT, sigintHandler);
    get_log_fp();
    rte_openlog_stream(fp);
    rte_log_set_global_level(RTE_LOG_INFO);
    Unackpkts_list = myLinkedListCreate();
    ctl_pkt_list = myLinkedListCreate();
    char     line[256] = {0};
    uint32_t flow_id = 0;
    uint32_t src_ip_segment1 = 0, src_ip_segment2 = 0, src_ip_segment3 = 0, src_ip_segment4 = 0;
    uint32_t dst_ip_segment1 = 0, dst_ip_segment2 = 0, dst_ip_segment3 = 0, dst_ip_segment4 = 0;
    uint16_t udp_src_port = 0, udp_dst_port = 0;
    uint32_t flow_size = 0;
    double   start_time;

    send_pool_yog1 = rte_pktmbuf_pool_create("SEND_POOL", 8192, 250, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (send_pool_yog1 == NULL) {
        rte_panic("Failed to create mempool.\n");
    }

    // Unackpkts_list = myLinkedListCreate();
    // for(i=0;i<MAX_CONCURRENT_FLOW;i++)    
    //     flow_flag[i] = 0;

    FILE *fd = fopen(flow_filename, "r");
    if (!fd)
        if(SHOW_DEBUG_INFO_yog1) printf("%s: no such file\n", flow_filename);
    //初始化端侧发送端信息
    while (fgets(line, sizeof(line), fd) != NULL) {
        remove_newline(line);
        sscanf(line, "%u %u %u %u %u %u %u %u %u %hu %hu %u %lf", &flow_id, 
            &src_ip_segment1, &src_ip_segment2, &src_ip_segment3, &src_ip_segment4, 
            &dst_ip_segment1, &dst_ip_segment2, &dst_ip_segment3, &dst_ip_segment4, 
            &udp_src_port, &udp_dst_port, &flow_size, &start_time);
        // sender_flows[flow_id].src_ip                    = RTE_IPV4(src_ip_segment1, src_ip_segment2, 
        //                                                   src_ip_segment3, src_ip_segment4);
        sender_flows[flow_id].src_ip                    = IPv4(src_ip_segment1, src_ip_segment2, 
                                                          src_ip_segment3, src_ip_segment4);
        // if(SHOW_DEBUG_INFO_yog1) printf("src ip = %u.%u.%u.%u\n", src_ip_segment1, src_ip_segment2, 
        //         src_ip_segment3, src_ip_segment4);
        // sender_flows[flow_id].dst_ip                    = RTE_IPV4(dst_ip_segment1, dst_ip_segment2, 
        //                                                   dst_ip_segment3, dst_ip_segment4);
        sender_flows[flow_id].dst_ip                    = IPv4(dst_ip_segment1, dst_ip_segment2, 
                                                          dst_ip_segment3, dst_ip_segment4);
        // if(SHOW_DEBUG_INFO_yog1) printf("dst ip = %u.%u.%u.%u\n", dst_ip_segment1, dst_ip_segment2, 
        //        dst_ip_segment3, dst_ip_segment4);
        sender_flows[flow_id].src_port                  = udp_src_port;
        sender_flows[flow_id].dst_port                  = udp_dst_port;
        sender_flows[flow_id].flow_size                 = flow_size;
        sender_flows[flow_id].start_time                = start_time;
        sender_flows[flow_id].rank_in_sender            = 0;
        sender_flows[flow_id].flow_finished             = 0;
        

        sender_flows[flow_id].sender_finish_time        = -1;//初始化
        sender_flows[flow_id].remain_size        = flow_size;//初始化
        sender_flows[flow_id].sender_unack_size         = 0;
        sender_flows[flow_id].sender_acklen               = 0;
        sender_flows[flow_id].sender_can_send_size      = flow_size;
        sender_flows[flow_id].sender_wait_count         = 0;
        sender_flows[flow_id].is_pre_grant              = 0;
        receiver_flows[flow_id].receiver_finish_time    = -1;
        //接收端在接收到INF_yog1O后要将信息落实全面
        // if(SHOW_DEBUG_INFO_yog1) printf("THIS IP  = %d %d %d %d\n",ipv4_hdr->dst_addr&0xFF,(ipv4_hdr->dst_addr&0xFF00)>>8,(ipv4_hdr->dst_addr&0xFF0000)>>16,(ipv4_hdr->dst_addr&0xFF000000)>>24);
        if (get_src_server_id(flow_id, sender_flows) == this_server_id_yog1)
            sender_total_flow_num_yog1_1++;

        if (get_dst_server_id(flow_id, sender_flows) == this_server_id_yog1)
            receiver_total_flow_num_yog1_1++;
        
        if (verbose_yog1 > 0) {
            if(SHOW_DEBUG_INFO_yog1) printf("Flow info: flow_id=%u, src_ip=%u, dst_ip=%u, "
                "src_port=%hu, dst_port=%hu, flow_size=%u, start_time=%lf\n",  
                flow_id, sender_flows[flow_id].src_ip, sender_flows[flow_id].dst_ip, 
                sender_flows[flow_id].src_port, sender_flows[flow_id].dst_port, 
                sender_flows[flow_id].flow_size, sender_flows[flow_id].start_time); 
        }

        if (flow_id == (uint32_t)total_flow_num_yog1-2)
            break;
    }
    /* find the first flow to start for this server */
    sender_next_unstart_flow_id_yog1 = -1;
    sender_next_unstart_flow_id_yog1 = find_next_unstart_flow_id();
    fclose(fd);

    sender_largeflow_arr = createSet();
    sender_smallflow_arr = createSet();
    receiver_actflow_arr = createSet();
    receiver_firmflow_arr = createSet();
    receiver_smallflow_arr = createSet();

    // for (int i=0; i<MAX_CONCURRENT_FLOW; i++) {
    //     sender_request_sent_flow_array[i] = -1;
    //     sender_active_flow_array[i] = -1;
    //     receiver_active_flow_array[i] = -1;
    // }

    if (verbose_yog1 > 0)
        if(SHOW_DEBUG_INFO_yog1) printf("Flow info summary: total_flow_num_yog1 = %d, sender_total_flow_num_yog1_1 = %d\n",
            total_flow_num_yog1, sender_total_flow_num_yog1_1);

#ifdef NEEDARBITER
    if (verbose_yog1 > 0)
        if(SHOW_DEBUG_INFO_yog1) printf("Arbiter_init begin...");

    for(int i=0;i<SERVERNUM;i++)
    {
        host_send_flows[i]              = createSet();
        host_sending_flow_id_yog1[i]         = 0;
        host_receiving_flow_id_yog1[i]       = 0;
        host_sender_ctl_type_yog1[i]         = unsure;
        host_receiver_ctl_type_yog1[i]       = unsure;
    }     
    arbiter_flows_set = createSet();

    if (verbose_yog1 > 0)
        if(SHOW_DEBUG_INFO_yog1) printf("Arbiter_init end.");
#endif
}

static void 
test_recv_pkt(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr);

static void 
test_recv_pkt(struct rte_tcp_hdr *transport_recv_hdr, struct rte_ipv4_hdr *ipv4_hdr) {
    uint8_t  pkt_type = transport_recv_hdr->PKT_TYPE_8BITS;
    Parsing_info* Par_info = get_parse_info(transport_recv_hdr,ipv4_hdr);
    if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"当前包类型为%d\n",pkt_type);
    if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"当前包的parse内容为:flow_id = %d flow_size = %d handing_flow_id = %d host_ctl_type = %d flow_remain_size = %d rank_in_sender = %d receiver_reclen = %d\n",Par_info->flow_id,Par_info->flow_size,Par_info->handing_flow_id,Par_info->host_ctl_type,Par_info->flow_remain_size,Par_info->rank_in_sender,Par_info->receiver_reclen);
    if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"\n");
}



/* Receive and process a burst of packets. */
int final_printed_yog1 = 0;
static void
recv_pkt(struct fwd_stream *fs)
{
    struct   rte_mbuf *pkts_burst[MAX_PKT_BURST];
    struct   rte_mbuf *mb;
    uint16_t nb_rx;
    struct   rte_ipv4_hdr *ipv4_hdr;
    struct   rte_tcp_hdr *transport_recv_hdr;
    uint8_t  l4_proto;
    uint8_t  pkt_type = 0;

#ifdef NEEDARBITER
    Parsing_info *Par_info;
#endif
    struct rte_ether_hdr *eth_hdr;



    /* Receive a burst of packets. */
    nb_rx = rte_eth_rx_burst(fs->rx_port, fs->rx_queue, pkts_burst, nb_pkt_per_burst);

    // printf("rx: %d\n", nb_rx);

    // struct rte_eth_rxq_info rxq_info;
    // uint32_t queue_size = rte_eth_rx_queue_count(fs->rx_port, fs->rx_queue);
    // rte_eth_rx_queue_info_get(fs->rx_port, fs->rx_queue, &rxq_info);
    // uint32_t queue_capacity = rxq_info.nb_desc;
    // printf("total: %u, remain: %u\n", queue_capacity, queue_capacity - queue_size);

    if (unlikely(nb_rx == 0))
        return;

#ifdef RTE_TEST_PMD_RECORD_BURST_STATS
    fs->rx_burst_stats.pkt_burst_spread[nb_rx]++;
#endif
    fs->rx_packets += nb_rx;

    /* Process a burst of packets. */
    for (int i = 0; i < nb_rx; i++) {
        mb = pkts_burst[i]; 
        // mb = rte_pktmbuf_clone(mb, mb->pool);
        ipv4_hdr = rte_pktmbuf_mtod_offset(mb, struct rte_ipv4_hdr *, L2_LEN);
        struct rte_ether_hdr *eth = rte_pktmbuf_mtod(mb, struct rte_ether_hdr*);
        if (ipv4_hdr->dst_addr != rte_cpu_to_be_32(ip_addr_array[this_server_id_yog1]))
        {
            print_ether_addr("pkt not for this, src_mac", &(eth->src_addr));
            print_ether_addr("pkt not for this, dst_mac", &(eth->dst_addr));
            printf("dst add = %d %d %d %d\n",ipv4_hdr->dst_addr&0xFF,(ipv4_hdr->dst_addr&0xFF00)>>8,(ipv4_hdr->dst_addr&0xFF0000)>>16,(ipv4_hdr->dst_addr&0xFF000000)>>24);
            // if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"rec pkt not for this - dst add = %d %d %d %d\n",ipv4_hdr->dst_addr&0xFF,(ipv4_hdr->dst_addr&0xFF00)>>8,(ipv4_hdr->dst_addr&0xFF0000)>>16,(ipv4_hdr->dst_addr&0xFF000000)>>24);
            rte_pktmbuf_free(mb);
            continue;
        }

        // print_ether_addr("for this, src_mac", &(eth->src_addr));
        //     print_ether_addr("for this, dst_mac", &(eth->dst_addr));
        //     printf("dst add = %d %d %d %d\n",ipv4_hdr->dst_addr&0xFF,(ipv4_hdr->dst_addr&0xFF00)>>8,(ipv4_hdr->dst_addr&0xFF0000)>>16,(ipv4_hdr->dst_addr&0xFF000000)>>24);

        l4_proto = ipv4_hdr->next_proto_id;
        if (l4_proto == IPPROTO_TCP) {
            transport_recv_hdr = rte_pktmbuf_mtod_offset(mb, struct rte_tcp_hdr *, L2_LEN + L3_LEN);
            pkt_type = transport_recv_hdr->PKT_TYPE_8BITS;

            int flow_src_id = -2;
            int flow_dst_id = -2;
            uint16_t flow_id = rte_be_to_cpu_16(transport_recv_hdr->FLOW_ID_16BITS);

            if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "\n-----------------recv new pkt----------------------\n");
            if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "recv flow id: %d src id:%d pkt_type: %x...\n", flow_id, get_src_server_id(flow_id, sender_flows), pkt_type);
            if(SHOW_SR_LOG)printf("[recv]recv pkt %x from %d, flow id: %d\n", pkt_type, rte_be_to_cpu_32(ipv4_hdr->src_addr)&0xFF, flow_id);
            if(SAVE_SR_LOG)fprintf(fp, "[recv]recv pkt %x from %d, flow id: %d\n", pkt_type, rte_be_to_cpu_32(ipv4_hdr->src_addr)&0xFF, flow_id);

            uint16_t new_id = rte_be_to_cpu_16(transport_recv_hdr->FLOW_ID_16BITS);
            // printf("old: %d, new: %d\n", flow_id, new_id);
            assert(flow_id == new_id);

            assert(pkt_type != 0);
            switch (pkt_type)
            {
            case PT_INF_yog1O:
            case PT_GRANT:
            case PT_DATA:
            case PT_ACK:
            case PT_CHANGE_RANK_1:
            case PT_CHANGE_RANK_2:
            case PT_REFUSE:
            case PT_WAIT:
            case PT_ORDER:
            case PT_FLOW_FINISH:
                    if (sender_flows[flow_id].flow_finished == 1 || receiver_flows[flow_id].flow_finished == 1) // 如果端侧结束
                    {
                        if (pkt_type == PT_FLOW_FINISH)
                        {
                            if (sender_flows[flow_id].sender_unack_size > 0 && sender_flows[flow_id].flow_finished == 1)
                            {
                                if (SAVE_DEBUG_INFO_yog1)
                                    fprintf(fp, "[Warn]recv flow_finish but sender_unack_size > 0, will clear it anyway.\n");
                                if (SHOW_DEBUG_INFO_yog1)
                                    printf("[Warn]recv flow_finish but sender_unack_size > 0, will clear it anyway.\n");
                                sender_flows[flow_id].sender_unack_size = 0;
                                sender_flows[flow_id].sender_can_send_size = 0;
                            }
                            send_ctl_pkt(PT_FLOW_FINISH_ACK, flow_id, 0);
                            continue;
                        }
                        if (sender_flows[flow_id].flow_finished == 1)
                            if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "[Warn]will send flow finish because recv 0x%x and sender flow_fnished in L2712, flow id is %d\n", pkt_type, flow_id);
                        else
                            if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "[Warn]will send flow finish because recv 0x%x and receiver flow_fnished in L2712, flow id is %d\n", pkt_type, flow_id);
                        send_ctl_pkt(PT_FLOW_FINISH, flow_id, 1);
                        continue;
                    }
                break;

            // CTL_PKT retransmit
            case PT_INF_yog1O_ACK:
            case PT_GRANT_ACK:
            case PT_CHANGE_RANK_1_ACK:
            case PT_CHANGE_RANK_2_ACK:
            case PT_FLOW_FINISH_ACK:
                if( myLinkedListDeletebyInfo_ctl(pkt_type & 0xef, flow_id) != 1)
                {
                    char *temp_string = malloc(sizeof(char) * 128);
                    sprintf(temp_string,"[Error][recv_pkt]Failed to release CTL_PKT retransmit timer. pkt: 0x%x, try to release 0x%x\n", pkt_type, pkt_type & 0xef);
                    LOG_PKT_INFO(transport_recv_hdr, ipv4_hdr, temp_string);
                }
                while(myLinkedListDeletebyInfo_ctl(pkt_type & 0xef, flow_id));
                continue;

#ifdef NEEDARBITER
            case ARBITER_PT_INF_yog1O:
            case ARBITER_PT_GRANT:
            case ARBITER_PT_REFUSE:
            case ARBITER_PT_FLOW_FINISH:
                Par_info = get_parse_info(transport_recv_hdr, ipv4_hdr);
                // if (contains(arbiter_flows_set, Par_info->flow_id))
                // {
                if (Par_info->flow_id < 0 || Par_info->flow_id >= total_flow_num_yog1)
                {
                    if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "[Error]recv a unexpected pkt\n");
                    continue;
                }
                    flow_src_id = get_src_server_id(Par_info->flow_id, sender_flows);
                    flow_dst_id = get_dst_server_id(Par_info->flow_id, sender_flows);
                // }

                break;
#endif

            default:
                break;
            }


            // // grant 也不该处理
            // if (pkt_type != ARBITER_PT_INF_yog1O && pkt_type != ARBITER_PT_FLOW_FINISH && pkt_type != ARBITER_PT_GRANT && pkt_type != ARBITER_PT_REFUSE)
            // {
            //     if (pkt_type != PT_INF_yog1O && pkt_type!=PT_GRANT && contains(receiver_actflow_arr, flow_id) != 1 && pkt_type != PT_DATA)
            //     {
            //         LOG_PKT_INFO(transport_recv_hdr, ipv4_hdr, "[Warn][recv_pkt]flow not in receiver_actflow_arr but get pkt.");
            //     }
            //     if (pkt_type != PT_INF_yog1O && pkt_type!=PT_GRANT && contains(receiver_actflow_arr, flow_id) != 1 && pkt_type != PT_DATA)
            //     {
            //         receiver_active_flow_num_yog1++;

            //         receiver_flows[flow_id].src_port = RTE_BE_TO_CPU_16(transport_recv_hdr->dst_port);
            //         receiver_flows[flow_id].dst_port = RTE_BE_TO_CPU_16(transport_recv_hdr->src_port);
            //         receiver_flows[flow_id].src_ip = rte_be_to_cpu_32(ipv4_hdr->dst_addr);
            //         receiver_flows[flow_id].dst_ip = rte_be_to_cpu_32(ipv4_hdr->src_addr);
            //         receiver_flows[flow_id].flow_size = sender_flows[flow_id].flow_size;
            //         receiver_flows[flow_id].rank_in_sender = rte_be_to_cpu_32(transport_recv_hdr->recv_ack);
            //         receiver_flows[flow_id].remain_size = rte_be_to_cpu_32(transport_recv_hdr->FLOW_REMAIN_SIZE);
            //         if (receiver_flows[flow_id].remain_size > sender_flows[flow_id].flow_size)
            //         {
            //             receiver_flows[flow_id].remain_size = sender_flows[flow_id].flow_size;
            //             if (SAVE_DEBUG_INFO_yog1)
            //                 fprintf(fp, "at info remain size error\n");
            //         }
            //         receiver_flows[flow_id].start_time = rte_rdtsc() / (double)hz;
            //         receiver_flows[flow_id].flow_finished = 0;
            //         receiver_flows[flow_id].receiver_recvlen = 0;
            //         receiver_flows[flow_id].receiver_finish_time = 0;
            //         receiver_flows[flow_id].unexcept_pkt_count = 0;

            //         addSet(receiver_actflow_arr, flow_id, receiver_flows);
            //     }
            // }

            // test_recv_pkt(transport_recv_hdr, ipv4_hdr);
            switch (pkt_type) {
                case PT_SYNC:
                    if (!sync_done_yog1)
                    {
                        recv_sync(transport_recv_hdr, ipv4_hdr);
                    }
                    break;
                case PT_INF_yog1O:
                    send_ctl_pkt(PT_INF_yog1O_ACK, flow_id, 0);
                    recv_info(transport_recv_hdr, ipv4_hdr);
                    break;
                case PT_GRANT:
                    send_ctl_pkt(PT_GRANT_ACK, flow_id, 0);
                    recv_grant(transport_recv_hdr, ipv4_hdr);
                    break;
                case PT_DATA:
                    recv_data(transport_recv_hdr, ipv4_hdr);
                    break;
                case PT_ACK:
                    recv_ack(transport_recv_hdr, ipv4_hdr);
                    break;
                case PT_CHANGE_RANK_1:
                    send_ctl_pkt(PT_CHANGE_RANK_1_ACK, flow_id, 0);
                    recv_change_rank1(transport_recv_hdr, ipv4_hdr);
                    break;
                case PT_CHANGE_RANK_2:
                    send_ctl_pkt(PT_CHANGE_RANK_2_ACK, flow_id, 0);
                    recv_change_rank2(transport_recv_hdr, ipv4_hdr);
                    break;
                case PT_REFUSE:
                    recv_refuse(transport_recv_hdr, ipv4_hdr);
                    break;
                case PT_WAIT:
                    recv_wait(transport_recv_hdr, ipv4_hdr);
                    break;
                case PT_ORDER:
                    recv_order(transport_recv_hdr, ipv4_hdr);
                    break;
                case PT_FLOW_FINISH:
                    send_ctl_pkt(PT_FLOW_FINISH_ACK, flow_id, 0);
                    recv_flow_finish(transport_recv_hdr, ipv4_hdr);
                    break;

#ifdef NEEDARBITER
                case ARBITER_PT_INF_yog1O:
                    // if (contains(arbiter_flows_set, Par_info->flow_id))
                    // RTE_LOG(WARNING, ACL, "why flow info twice get!!!!!!!");
                    // Arbiter_record_new_flow_info(Par_info, transport_recv_hdr, ipv4_hdr);   // 记录新流的信息
                    // addSet(arbiter_flows_set, Par_info->flow_id, sender_flows);            // 加入新流的列表
                    // flow_src_id = get_src_server_id(Par_info->flow_id, sender_flows);
                    if(SHOW_DEBUG_INFO_yog1) printf("flow_src_id: %d, flow_dst_id: %d\n", flow_src_id, flow_dst_id);
                    assert(flow_src_id > 0);
                    addSet(host_send_flows[flow_src_id], Par_info->flow_id, sender_flows); // 加入对应发送端的队列中
                    break;
                case ARBITER_PT_GRANT:
                    if(SHOW_DEBUG_INFO_yog1) printf("flow_src_id: %d, flow_dst_id: %d\n", flow_src_id, flow_dst_id);
                    assert(flow_src_id > 0 && flow_dst_id > 0);
                    host_sender_ctl_type_yog1[flow_src_id] = self_src;
                    host_sending_flow_id_yog1[flow_src_id] = Par_info->flow_id;
                    break;
                case ARBITER_PT_REFUSE:
                    if(SHOW_DEBUG_INFO_yog1) printf("flow_src_id: %d, flow_dst_id: %d\n", flow_src_id, flow_dst_id);
                    assert(flow_src_id > 0 && flow_dst_id > 0);
                    if (rte_be_to_cpu_32(ipv4_hdr->src_addr) == ip_addr_array[flow_src_id]) // 发送端->接收端的refuse
                    {
                    if (host_receiving_flow_id_yog1[flow_dst_id] == Par_info->flow_id)
                    {
                        host_receiving_flow_id_yog1[flow_dst_id] = 0;
                        host_receiver_ctl_type_yog1[flow_dst_id] = unsure;
                    }
                    }
                    else if (rte_be_to_cpu_32(ipv4_hdr->src_addr) == ip_addr_array[flow_dst_id])
                    { // 接收端->发送端的refuse
                    if (host_sending_flow_id_yog1[flow_src_id] == Par_info->flow_id)
                    {
                        host_sending_flow_id_yog1[flow_src_id] = 0;
                        host_sender_ctl_type_yog1[flow_src_id] = unsure;
                    }
                    }
                    else
                    RTE_LOG(WARNING, ACL, "why refuse get wrong id!!!!!!!");
                    break;
                case ARBITER_PT_FLOW_FINISH:
                    if((flow_src_id <= 0 || flow_dst_id <= 0)) printf("flow_src_id: %d, flow_dst_id: %d\n", flow_src_id, flow_dst_id);
                    assert(flow_src_id > 0 && flow_dst_id > 0);
                    if (host_sending_flow_id_yog1[flow_src_id] == Par_info->flow_id)
                    {
                    host_sending_flow_id_yog1[flow_src_id] = 0;
                    host_sender_ctl_type_yog1[flow_src_id] = unsure;
                    }
                    removeElement(host_send_flows[flow_src_id], Par_info->flow_id);
                    break;
#endif
                default:
                    break;
            }

#ifdef NEEDARBITER
            if (pkt_type & 0x40)
            {
                if(rte_be_to_cpu_32(ipv4_hdr->src_addr) == ip_addr_array[flow_src_id])//发送端发送的数据包
                    Syn_host_state(Par_info, 0);
                else if(rte_be_to_cpu_32(ipv4_hdr->src_addr) == ip_addr_array[flow_dst_id])
                    Syn_host_state(Par_info, 1);
                else
                    RTE_LOG(WARNING, ACL, "sync get wrong id");
            }
                    
#endif
            new_id = rte_be_to_cpu_16(transport_recv_hdr->FLOW_ID_16BITS);
            // printf("old: %d, new: %d\n", flow_id, new_id);
            assert(flow_id == new_id);
        }
        rte_pktmbuf_free(mb);
#ifdef NEEDARBITER
	// if (pkt_type & 0x40)
    //    free(Par_info);
#endif
        if(SAVE_DEBUG_INFO_yog1) fprintf(fp, "---------------------- recv one packet finished --------------------------------\n");
    }

#ifdef NEEDARBITER
    if (pkt_type & 0x40)
    {
        // if(SHOW_DEBUG_INFO_yog1) printf("enter Try_fill_bandwidth\n");
        Try_fill_bandwidth();
        // if(SHOW_DEBUG_INFO_yog1) printf("exit Try_fill_bandwidth\n");
    }
#endif

    if (nb_rx > 0)
    {
        show_host_info();
        // printf("1nb_rx: %d\n", nb_rx);
    }
    else if (final_printed_yog1 == 0)
    {
        printf("nb_rx: %d\n", nb_rx);
        show_host_info();
        final_printed_yog1 = 1;
    }
}


/* Sender send a burst of packets */
static void
sender_send_pkt(void)
{
    for(int i=0; i<sender_current_burst_size_yog1; i++)
    {
        struct rte_mbuf *mb = sender_pkts_burst[i];
        struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(mb, struct rte_ether_hdr *);
        struct rte_ipv4_hdr *ipv4_hdr = rte_pktmbuf_mtod_offset(mb, struct rte_ipv4_hdr *, L2_LEN);
        struct rte_tcp_hdr * transport_hdr = (struct rte_tcp_hdr *)(ipv4_hdr + 1);

        printf("[send] send ip %u eth %x\n", ipv4_hdr->dst_addr, eth_hdr->dst_addr.addr_bytes[5]);
        // if(SHOW_SR_LOG) printf("[send]Will send pkt %x to server %u, flow id: %d\n", transport_hdr->PKT_TYPE_8BITS, rte_be_to_cpu_32(ipv4_hdr->dst_addr)&0xFF, rte_be_to_cpu_16(transport_hdr->FLOW_ID_16BITS));
        // if(SAVE_SR_LOG) fprintf(fp, "[send]Will send pkt %x to server %u, flow id: %d\n", transport_hdr->PKT_TYPE_8BITS, rte_be_to_cpu_32(ipv4_hdr->dst_addr)&0xFF, rte_be_to_cpu_16(transport_hdr->FLOW_ID_16BITS));
    }

    uint16_t nb_pkt = sender_current_burst_size_yog1;
    uint16_t nb_tx = rte_eth_tx_burst(global_fs->tx_port, global_fs->tx_queue, sender_pkts_burst, nb_pkt);
    // printf("sent get %d pkt\n",nb_pkt);
    if (unlikely(nb_tx < nb_pkt) && global_fs->retry_enabled) {
        uint32_t retry = 0;
        while (nb_tx < nb_pkt && retry++ < burst_tx_retry_num) {
            if (verbose_yog1 > 1) {
                print_elapsed_time();
                printf(" - sender_send_pkt, retry = %u\n", retry);
            }
            rte_delay_us(burst_tx_delay_time);
            nb_tx += rte_eth_tx_burst(global_fs->tx_port, global_fs->tx_queue, 
                                        &sender_pkts_burst[nb_tx], nb_pkt - nb_tx);
        }
    }
    // rte_pktmbuf_free_bulk(sender_pkts_burst,sender_current_burst_size_yog1);
    global_fs->tx_packets += nb_tx;
    sender_current_burst_size_yog1 = 0;
}

/* Receiver send a burst of packets */
static void
receiver_send_pkt(void)
{
    uint16_t nb_pkt = receiver_current_burst_size_yog1;
    uint16_t nb_tx = rte_eth_tx_burst(global_fs->tx_port, global_fs->tx_queue, receiver_pkts_burst, nb_pkt);

    if (unlikely(nb_tx < nb_pkt) && global_fs->retry_enabled) {
        uint32_t retry = 0;
        while (nb_tx < nb_pkt && retry++ < burst_tx_retry_num) {
            if (verbose_yog1 > 1) {
                print_elapsed_time();
                if(SHOW_DEBUG_INFO_yog1) printf(" - receiver_send_pkt, retry = %u\n", retry);
            }
            rte_delay_us(burst_tx_delay_time);
            nb_tx += rte_eth_tx_burst(global_fs->tx_port, global_fs->tx_queue, 
                                        &receiver_pkts_burst[nb_tx], nb_pkt - nb_tx);
        }
    }

    global_fs->tx_packets += nb_tx;
    receiver_current_burst_size_yog1 = 0;
}

/* Start one warm up flow - send data without receive */
// static void
// start_warm_up_flow(void)
// {
//     int flow_id = sender_next_unstart_flow_id_yog1;
//     sender_next_unstart_flow_id_yog1 = find_next_unstart_flow_id();
    
//     while (elapsed_cycle/(double)hz < warm_up_time_yog1) {
//         if(sender_current_burst_size_yog1<5)
//             construct_data(flow_id, 2);
//         elapsed_cycle = rte_rdtsc() - start_cycle;
//     }

//     sender_send_pkt();
//     // sender_finished_flow_num_yog1++;

//     if (verbose_yog1 > 0) {
//         print_elapsed_time();
//         if(SHOW_DEBUG_INFO_yog1) printf(" - start_warm_up_flow %d done\n", flow_id);
//     }
// }

static void
start_new_flow(void)
{
    double now;
    int flow_id, request_retrans_check, retransmit_flow_index, max_request_retrans_check;

    // if (sender_current_burst_size_yog1 > BURST_THRESHOLD) {
    //     sender_send_pkt();
    //     sender_current_burst_size_yog1 = 0;
    // }


    /* Start new flow */
    if (sender_next_unstart_flow_id_yog1 < total_flow_num_yog1)
    {

        flow_id = sender_next_unstart_flow_id_yog1;
        now = rte_rdtsc() / (double)hz;

        while ((sender_flows[flow_id].start_time + flowgen_start_time + warm_up_time_yog1) <= now)
        { // 如果当前流开始了

            if (verbose_yog1 > 0)
            {
                print_elapsed_time();
                if(SHOW_DEBUG_INFO_yog1) printf(" - start_new_flow %d\n", flow_id);
            }

            if (sender_flows[flow_id].flow_size > RTT_BYTES)
            {                                                           // 如果是大流
                int rank_in_sender = add_sender_largeflow_arr(flow_id); // 尝试加入大流列表
                char *temp_string = malloc(sizeof(char) * 128);
                sprintf(temp_string, "[start_new_flow]flow start with rank = %d active flow = %d  smallest_flowid = %d its remain size = %d ", rank_in_sender, sender_active_flow_num_yog1, getset_smallest_flowid(sender_largeflow_arr), sender_flows[getset_smallest_flowid(sender_largeflow_arr)].remain_size);
                LOG_DEBUG(1, __FILE__, __LINE__, flow_id, temp_string);

                if (rank_in_sender == -1) // 如果该流存在过，直接返回
                {
                    RTE_LOG(WARNING, ACL, "Error: %s\n", "start new flow get same flow id");
                    continue;
                }
                sender_flows[flow_id].rank_in_sender = rank_in_sender;
                send_ctl_pkt(PT_INF_yog1O, flow_id, 1);
            }
            else
            {
                add_sender_smallflow_arr(flow_id);
            }
            // send_ctl_pkt(PT_INF_yog1O,flow_id,0);
            sender_next_unstart_flow_id_yog1 = find_next_unstart_flow_id();
            if (sender_next_unstart_flow_id_yog1 < total_flow_num_yog1)
            {
                flow_id = sender_next_unstart_flow_id_yog1;
            }
            else
            {
                break;
            }
        }
    }
}

static void 
test_start_new_flow(void);

static void 
test_start_new_flow(void) {
    SetNode* p_large;
    SetNode* p_small;
    int flow_id;
    p_large = sender_largeflow_arr->head;
    p_small = sender_smallflow_arr->head;
    while(p_large != NULL) {
        flow_id = p_large->flow_id;
        if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"大流 flow_id = %d flow_size = %d\n",flow_id,sender_flows[flow_id].flow_size);
        p_large = p_large->next;
    }
        if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"\n");
    while(p_small != NULL) {
        flow_id = p_small->flow_id;
        if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"小流 flow_id = %d flow_size = %d\n",flow_id,sender_flows[flow_id].flow_size);
        p_small = p_small->next;
    }
        if(SAVE_DEBUG_INFO_yog1) fprintf(fp,"\n");
}

static void
print_fct(void)
{

        int print_option = 1;
        // printf("Sender FCT:\n");
        // for (int i = 0; i < total_flow_num_yog1; i++)
        // {
        // if (sender_flows[i].src_ip == ip_addr_array[this_server_id_yog1])
        // {
        //     printf("src: %d, dest: %d ", get_src_server_id(i, sender_flows), get_dst_server_id(i, sender_flows));
        //     if (sender_flows[i].flow_finished == 1)
        //         printf("flow %d - fct = %lf\n", i,
        //                sender_flows[i].sender_finish_time - sender_flows[i].start_time);
        //     else if (print_option == 1)
        //         printf("flow %d - flow_size = %u, remain_size = %u\n", i, sender_flows[i].flow_size,
        //                sender_flows[i].remain_size);
        // }
        // }

        printf("receiver FCT:\n");
        printf("flow_id, flow_size, fct\n");
        fprintf(fct_fp, "flow_id, flow_size, fct\n");
        for (int i = 0; i < total_flow_num_yog1; i++)
        {
        if (receiver_flows[i].dst_ip == ip_addr_array[this_server_id_yog1])
        {
            // printf("src: %d, dst: %d ", get_src_server_id(i, receiver_flows), get_dst_server_id(i, receiver_flows));
            // fprintf(fp, "src: %d, dst: %d ", get_src_server_id(i, receiver_flows), get_dst_server_id(i, receiver_flows));
            if (receiver_flows[i].flow_finished == 1)
            {
                printf("%d, %u, %lf\n", i, receiver_flows[i].flow_size,
                       receiver_flows[i].receiver_finish_time - receiver_flows[i].start_time);
                fprintf(fct_fp, "%d, %u, %lf\n", i, receiver_flows[i].flow_size,
                       receiver_flows[i].receiver_finish_time - receiver_flows[i].start_time);
            }

            else if (print_option == 1)
            {
                printf("[unfinished] flow %d - flow_size = %u, remain_size = %u\n", i, receiver_flows[i].flow_size,
                       receiver_flows[i].remain_size);
                fprintf(fct_fp, "[unfinished] flow %d - flow_size = %u, remain_size = %u\n", i, receiver_flows[i].flow_size,
                        receiver_flows[i].remain_size);
            }
        }
        }
}

/* flowgen packet_fwd main function */
static void
main_flowgen(struct fwd_stream *fs)
{
    /* Initialize global variables */
    rte_delay_ms(1000);//起始等一会

    global_fs = fs;
    hz = rte_get_timer_hz(); 
    sender_flows = rte_zmalloc("testpmd: struct flow_info",
            total_flow_num_yog1*sizeof(struct flow_info), RTE_CACHE_LINE_SIZE);//初始化内存
    receiver_flows = rte_zmalloc("testpmd: struct flow_info",
            total_flow_num_yog1*sizeof(struct flow_info), RTE_CACHE_LINE_SIZE);
#ifdef NEEDARBITER
    // arbiter_flows = rte_zmalloc("testpmd: struct flow_info",
    //         total_flow_num_yog1*sizeof(struct flow_info), RTE_CACHE_LINE_SIZE);//初始化内存
#endif

    /* Read basic info of server number, mac and ip */
     printf("\nEnter read_config...\n\n");
    read_config();//读取参数，把IP地址和MAC地址读取到对应的数组中
     printf("\nExit read_config...\n\n");
    
    /* Init flow info */
     printf("\nEnter init...\n\n");
    init();//初始化全局流数据，以及一些数组的初始化
     printf("\nExit init...\n\n");

     start_cycle = rte_rdtsc();
     elapsed_cycle = 0;
     sync_done_yog1 = 0;
     flowgen_start_time = start_cycle / (double)hz;
     double last_time = flowgen_start_time;
     while (elapsed_cycle / (double)hz < warm_up_time_yog1)
     {
        recv_pkt(fs);
        double now = rte_rdtsc() / (double)hz;
        if (now - last_time > 1)
        {
            for (int i = SERVERNUM-1; i >0; i--)
            {
                 if (i != this_server_id_yog1)
                 {
                    // if(i==8)
                    // construct_sync(8);
                    construct_sync(i);
                 }
                 if (verbose_yog1 > 0)
                 {
                    print_elapsed_time();
                    printf(" - sync pkt sent of server_id %d\n", i);
                 }
            }
            // sender_send_pkt();
            last_time = now;
            exit(0);
        }
        if (sync_done_yog1 == 1)
        {
            warm_up_time_yog1 = elapsed_cycle / (double)hz;
            break;
        }
     }
     printf("Warm up and sync delay = %.2lf sec\n", warm_up_time_yog1);
     printf("\nExit warm up and sync loop...\n\n");

     /* Main flowgen loop */
     printf("\nEnter main_flowgen loop...\n\n");
     printf("MAX_TIME = %d sec after warm up\n", MAX_TIME);
    double main_start_time = rte_rdtsc() / (double)hz;
    double total_main_time=0;
    double loop_time = warm_up_time_yog1 + MAX_TIME;
    int    main_flowgen_loop = 1;
    double total_time_phase1 = 0;
    double total_time_phase2 = 0;
    double total_time_phase3 = 0;
    double now;
    double last_probe = 0;
    long int loop_num = 0;

    double last_send = 0;
    // start_senddata_timer();
    printf("after new loop lcore id = %d\n", rte_lcore_id());
    int sender_finished_last = 0, receiver_finished_last = 0;
    while (elapsed_cycle < loop_time * hz)
    {
        loop_num++;
        //  printf("enter start_new_flow\n");
        start_new_flow();
        //  printf("exit start_new_flow\n");
        total_time_phase1 += rte_rdtsc() / (double)hz - now;


        total_time_phase2 += rte_rdtsc() / (double)hz - now;
        //  printf("enter recv_pkt\n");
        recv_pkt(fs);

        //  printf("exit recv_pkt\n");
        // now = rte_rdtsc() / (double)hz;
        // if(now-last_send>TIMER_INTERVAL_MS){
            send_data();
        //     last_send = now;
        // }

        total_time_phase3 += rte_rdtsc() / (double)hz - now;


        if (verbose_yog1 > 2) {
            print_elapsed_time();
             printf(" - exit  send_grant...\n\n");
        }

        elapsed_cycle = rte_rdtsc() - start_cycle;

        if(loop_num % 10000000)
        {
            if(sender_finished_flow_num_yog1 != sender_finished_last || receiver_finished_flow_num_yog1 != receiver_finished_last)
                printf("sender: %d/%d, receiver: %d/%d\n", sender_finished_flow_num_yog1, sender_total_flow_num_yog1_1, receiver_finished_flow_num_yog1, receiver_total_flow_num_yog1_1);
            sender_finished_last = sender_finished_flow_num_yog1;
            receiver_finished_last = receiver_finished_flow_num_yog1;

            if (sender_finished_flow_num_yog1 == sender_total_flow_num_yog1_1 && receiver_finished_flow_num_yog1 == receiver_total_flow_num_yog1_1)
            {
                now = rte_rdtsc() / (double)hz;
                total_main_time = now - main_start_time;
                // break;
            }
        }

        // if (loop_num == 1 || loop_num == 10 || loop_num == 100 || loop_num == 1000 || 
        //     loop_num == 10000 || loop_num == 100000 || loop_num == 1000000 || loop_num == 10000000 ||
        //     loop_num == 100000000 || loop_num == 1000000000 || loop_num == 10000000000)
        //     {
        //         // test_start_new_flow();
        //          printf("loop_num=%ld, total/average time in phase 1/2/3: %lf/%lf/%lf %lf/%lf/%lf\n",  
        //         loop_num, total_time_phase1, total_time_phase2, total_time_phase3,
        //         total_time_phase1/(double)loop_num, total_time_phase2/(double)loop_num, 
        //         total_time_phase3/(double)loop_num);
        //     }
    }
    printf("\nExit main_flowgen loop...\n\n");

    // send_timer_end();

    printf("\nEnter print_fct...\n\n");
    print_fct();
    printf("\nExit print_fct...\n\n");

    printf("total time(not accurate): %.2fs\n", total_main_time);

    exit(0);
}

struct fwd_engine yog1_engine = {
    .fwd_mode_name  = "yog1",
    .port_fwd_begin = NULL,
    .port_fwd_end   = NULL,
    .packet_fwd     = main_flowgen,
};
