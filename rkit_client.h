#ifndef CLIENT_H
#define CLIENT_H

#include "rkit_common.h"

#define CL "RKIT client: "

struct rkit_client_handler {
    /*
     * All the following variables are used for the address configuration
    */
    char *addr_str;                                                                 /* dst addr in string format */
    u8 addr[16];                                                                    /* dst addr in NBO */
    uint8_t addr_type;                                                              /* ADDR_FAMILY - IPv4/V6 */
    uint16_t port;                                                                  /* dst port in NBO */
    enum rkit_state *state;                                                         /* state of the connection */

    /* Memory region */     
    uint32_t *remote_rkeys;                                                         /* remote  RKEY */
    uint64_t *remote_addrs;                                                         /* remote TO */
    uint32_t *remote_lens;                                                          /* remote LEN */

    // a recieve work request to get rkit_data from server
    struct ib_recv_wr recv_wr;                                                      /* receive work request */
    struct ib_sge recv_sge;                                                         /* receive scatter/gather element */
    struct rkit_data recv_buf;                                                      /* data to receive */
    u64 recv_dma_addr;                                                              /* dma address of the receive buffer */

    /* Queues */
    struct ib_cq **cq;                                                              /* completion queues */
    struct ib_pd *pd;                                                               /* protection domain */
    struct ib_qp **qp;                                                              /* queue pairs */

    /* Connection managment objects */
    struct rdma_cm_id **cm_id;                                                      /* connection managers */
    wait_queue_head_t sem;                                                          /* wait queue for the connection */
}; /* Object of rkit client handler */

int rkit_init_client(const char *, const uint16_t);
struct ib_send_wr* rkit_create_send_batch(struct rkit_work_request* wrs);
int rkit_transfer(uint8_t thread_number, struct ib_send_wr* wr, uint8_t wait_for_wc);
void rkit_destroy_client(void);
struct rkit_client_handler* rkit_get_client_cb(void);
struct rkit_client_handler * rkit_get_client_handler(void);
#endif

