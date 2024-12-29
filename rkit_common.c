#include "rkit_common.h"

unsigned int send_batch_size;
unsigned int write_batch_size;
unsigned int read_batch_size;
unsigned int qp_mode;                      /* the QP mode options: RC,UC (when two sided: UC,RC)
                                            *                             when one sided: RC */
unsigned int number_of_connection_server;  // number of connections in server for each connection a qp will be created
unsigned int number_of_connection_client;  // number of connections in client for each connection a qp will be created
unsigned int maximum_send_wr_size;         /* maximum number of work requests in the send queue 
                                            * the larger the bigger the qp ==> more memory allocation on HCA */
unsigned int maximum_recv_wr_size;         /* maximum number of work requests in the reveive queue 
                                            * the larger the bigger the qp ==> more memory allocation on HCA */
unsigned int maximum_cqe_size;             /* maximum number of complete work entites in completion queue
                                            * the larger the bigger the qp ==> more memory allocation on HCA */
unsigned int maximum_inline_size;          /* only packets below this size will be transfered with inline flag (IB_SEND_INLINE)
                                            * hardware dependent and only used in send and write operations */
unsigned int poll_cq_count;                /* number of work completions retrieved from ib_poll_cq
                                            * the higher the value the more work completion can be recieved per function call */
struct semaphore server_client_sync1;
struct semaphore server_client_sync2;

void rkit_init_params(
    unsigned int _number_of_connection_server,
    unsigned int _number_of_connection_client,
    unsigned int _maximum_send_wr_size,
    unsigned int _maximum_recv_wr_size,
    unsigned int _maximum_cqe_size,
    unsigned int _maximum_inline_size,
    unsigned int _poll_cq_count
)
{
    // init semaphores 
    sema_init(&server_client_sync1, 0);
    sema_init(&server_client_sync2, 0);

    // Initialize the variables with the provided values
    number_of_connection_server = _number_of_connection_server;
    number_of_connection_client = _number_of_connection_client;
    maximum_send_wr_size = _maximum_send_wr_size;
    maximum_recv_wr_size = _maximum_recv_wr_size;
    maximum_cqe_size = _maximum_cqe_size;
    maximum_inline_size = _maximum_inline_size;
    poll_cq_count = _poll_cq_count;
}
