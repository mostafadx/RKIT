#ifndef COMMON_H
#define COMMON_H

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>
#include<linux/net.h>
#include <linux/in.h> 
#include <linux/inet.h>
#include <linux/moduleparam.h>
#include <linux/time.h>



#define ENABLE_TIME_LOG 1
#define ENABLE_DEBUG_LOG 1
#define ENABLE_INFINTE_LOOP_PROTECTION 1
#define TIME_LOG if (ENABLE_TIME_LOG) printk
#define DEBUG_LOG if (ENABLE_DEBUG_LOG) printk
#if ENABLE_INFINTE_LOOP_PROTECTION == 1
#define RKIT_MAX_TRY_LOOP 50000000               /* only used in infinite loop protection mode to avoid geting stuck in infinite loop,
                                                  * thus we get rid of time consuming reboot when developing the code */                                          
#endif              
#define RKIT_MAX_RETRIES 20                       // max number of retries for connecting to server
#define RKIT_RETRY_INTERVAL_MSEC 1000             // delay between client retry connection to server (millisecond)
#define RKIT_ENABLE_BLOCKING_SEND 1               /* 0: Just add the work request to the send queue
                                                   * 1: after addding the work request to send queue wait for work completion */
#define RKIT_QP_MODE IB_QPT_RC                    /* the QP mode options: RC,UC (when two sided: UC,RC)
                                                   *                             when one sided: RC */
#define RKIT_MAX_PAGE_SIZE  PAGE_SIZE

extern unsigned int number_of_connection_server;  // number of connections in server for each connection a qp will be created
extern unsigned int number_of_connection_client;  // number of connections in client for each connection a qp will be created
extern unsigned int maximum_send_wr_size;         /* maximum number of work requests in the send queue 
                                                   * the larger the bigger the qp ==> more memory allocation on HCA */
extern unsigned int maximum_recv_wr_size;         /* maximum number of work requests in the reveive queue 
                                                   * the larger the bigger the qp ==> more memory allocation on HCA */
extern unsigned int maximum_cqe_size;             /* maximum number of complete work entites in completion queue
                                                   * the larger the bigger the qp ==> more memory allocation on HCA */
extern unsigned int maximum_inline_size;          /* only packets below this size will be transfered with inline flag (IB_SEND_INLINE)
                                                   * hardware dependent and only used in send and write operations */
extern unsigned int poll_cq_count;                /* number of work completions retrieved from ib_poll_cq
                                                     the higher the value the more work completion can be recieved per function call */
extern struct semaphore server_client_sync1;
extern struct semaphore server_client_sync2;

#define ntohll(x) cpu_to_be64((x))
#define htonll(x) cpu_to_be64((x))
typedef unsigned long long ull;

/*
 * This enum is to handle rdma connection state in both client/server
 */
enum rkit_state
{
    IDLE = 1,
    CONNECT_REQUEST,        //2
    ADDR_RESOLVED,          //3
    ROUTE_RESOLVED,         //4
    CONNECTED,              //5
    ERROR                   //6
};

enum rkit_operation
{
    SEND_OP = 0,
    WRITE_OP,                  // 1
    READ_OP,                   // 2
    RECV_OP,                   // 3
    WRITE_IMM_OP,              // 4
    READ_WITH_NOTIFICATION_OP, // 5
    SEND_IMM_OP                // 6
};

/**
 * @brief Structure for transferring RDMA-related data from server to client.
 * 
 * The rkit_data structure is used to transfer information related to Remote Direct Memory
 * Access (RDMA) from the server to the client. It includes fields representing synchronization
 * tokens, local and remote port information, an identifier, buffer address, remote key (rkey),
 * size, and type of the data being transferred.
 * 
 * @note This structure is designed for communication between RDMA-enabled server and client
 * applications, facilitating the exchange of necessary information for RDMA operations.
 * 
 * @note The fields in this structure play vital roles in RDMA communication, such as identifying
 * the data, specifying buffer locations, and providing access permissions through the rkey.
 * 
 * @note This structure is only used in rdma one-sided operations (READ & WRITE). 
 * 
 * @note Each field's purpose:
 *        - sync_t: Synchronization token for coordination between server and client.
 *        - local_port_t: Local port information for the RDMA connection.
 *        - remote_port_t: Remote port information for the RDMA connection.
 *        - id: Identifier or tag associated with the data being transferred.
 *        - buf: Buffer address where the data is located.
 *        - rkey: Remote key providing access permissions to the data.
 *        - size: Size of the data being transferred.
 *        - type: Type information specifying the nature or purpose of the data.
 */
struct rkit_data
{
    uint64_t sync_t;
    uint64_t local_port_t;
    uint64_t remote_port_t;
    uint64_t id;
    uint64_t buf;
    uint32_t rkey;
    uint32_t size;
    uint32_t type;
};

struct rkit_work_request
{
    size_t length;
    u64 virtual_address;
    enum rkit_operation operation;
    u64  remote_addr;
    struct rkit_work_request *next;
    u32 imm_data;
};

void rkit_init_params
(
    unsigned int _number_of_connection_server,
    unsigned int _number_of_connection_client,
    unsigned int _maximum_send_wr_size,
    unsigned int _maximum_recv_wr_size,
    unsigned int _maximum_cqe_size,
    unsigned int _maximum_inline_size,
    unsigned int _poll_cq_count
);
#endif