
#ifndef SERVER_H
#define SERVER_H

#include "rkit_common.h"

#define SR "RKIT server: "

struct rkit_server_handler
{
    /*
     * State and address configuration
     */
    char *addr_str;                                                               /* dst addr in string format */
    u8 addr[16];                                                                  /* dst addr in NBO */
    uint8_t addr_type;                                                            /* ADDR_FAMILY - IPv4/V6 */
    uint16_t port;                                                                /* dst port in NBO */
    enum rkit_state* state;                                                       /* state of the connection */

    /*
    * Server thread
    */
    struct task_struct *thread;                                                   /* thread for server to run forever*/

    // a send work request to send rkit_data to client
    struct ib_send_wr send_wr;                                                    /* send work request */
    struct ib_sge send_sge;                                                       /* send scatter/gather element */
    struct rkit_data send_buf;                                                    /* data to send */
    u64 send_dma_addr;                                                            /* dma address of the send buffer */
    
    /* Memory region */     
    int page_list_len;                                                            /* length page list */
    struct ib_reg_wr reg_mr_wr;                                                   /* register memory region work request */
    struct ib_mr **reg_mrs;                                                         /* memory region for the register memory region */                                                                   /* key for the memory region */
    u64 *dmas;                                                                    /* dma's from memory_pages that will be assigned to memory region*/ 

    struct ib_mr *reg_mr;

    /* queues */        
    struct ib_cq **cq;                                                            /* completion queue */
    struct ib_pd *pd;                                                             /* protection domain */
    struct ib_qp **qp;                                                            /* queue pair */

    /* objects of connection managment */       
    struct rdma_cm_id **cm_id;                                                    /* connection on client side, listener on server side. */
    struct rdma_cm_id **child_cm_id;                                              /* connection on server side */
    wait_queue_head_t sem;                                                        /* wait queue for the connection */

}; /* object of rkit server handler */

typedef void (*rkit_processor)(struct rkit_server_handler *);

int rkit_init_server(void *);
int rkit_start_server(const char *, const uint16_t, rkit_processor);
void rkit_stop_server(void);
struct rkit_server_handler *rkit_get_server_cb(void);
struct rkit_server_handler *rkit_get_server_handler(void);
inline int rkit_post_recv_wr(struct ib_recv_wr*);
struct ib_recv_wr* rkit_create_recv_batch(struct rkit_work_request*); 
#endif