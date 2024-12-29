#include "rkit_client.h"

static struct rkit_client_handler *cb;
static int connection_number;

/**
* @brief Fill sockaddr structure with address information.
*
* @param sin Pointer to a sockaddr_storage structure to be filled.
*
* @note This function may modify global variables related to address and port.
*
* @return void
*/
static void fill_sockaddr(struct sockaddr_storage *sin)
{
    struct sockaddr_in *sin4;
    memset(sin, 0, sizeof(*sin));
    sin4 = (struct sockaddr_in *)sin;
    sin4->sin_family = cb->addr_type;
    memcpy((void *)&sin4->sin_addr.s_addr, cb->addr, 4);
    sin4->sin_port = cb->port;
}

/**
 * @brief Free the resources associated with RDMA Queue Pairs (QPs).
 * 
 * This function releases the resources allocated for RDMA Queue Pairs (QPs) and their
 * associated Completion Queues (CQs). It iterates through the array of QPs and CQs in the
 * control block (cb) and destroys each one. The purpose is to clean up and release
 * resources when they are no longer needed, preventing memory leaks.
 * 
 * @note This function assumes the existence of a control block (cb) with arrays of QPs and CQs.
 * 
 * @note The function logs debug information, indicating the successful release of QPs.
 * 
 * @note It is recommended to call this function when the RDMA communication is completed
 * or when the application is shutting down to release associated resources.
 * 
 * @note This function does not handle freeing other resources in the control block; it is
 * specific to the cleanup of QPs and CQs.
 */
static void rkit_free_qp(void)
{
    int i;
    for (i = 0; i < number_of_connection_client;i++)
    {
        if(cb->cq && cb->cq[i])
            ib_destroy_cq(cb->cq[i]);
        if(cb->qp && cb->qp[i])
            ib_destroy_qp(cb->qp[i]);
    }
    DEBUG_LOG(CL "QP(s) got free.");
}

static void rkit_free_buffers(void)
{
}

/**
 * @brief Destroy the RDMA client and release associated resources.
 * 
 * This function performs cleanup tasks for the RDMA client, including freeing RDMA Queue Pairs (QPs),
 * releasing associated buffers, and deallocating the control block (cb) object.
 * 
 * @note This function assumes the existence of a control block (cb) with necessary information,
 * including RDMA Queue Pairs (QPs) and associated buffers.
 * 
 * @note The function releases resources associated with RDMA communication, freeing QPs,
 * freeing buffers, and deallocating the control block object.
 * 
 * @note The function logs information about the client exit process.
 * 
 * @note It is recommended to call this function when the RDMA client is no longer needed, such as
 * during application shutdown, to ensure proper resource cleanup.
 */
void rkit_destroy_client(void)
{
    printk(CL "Client exit...\n");   
    rkit_free_qp();
    rkit_free_buffers();
    kfree(cb);
}

struct rkit_client_handler *rkit_get_client_cb(void)
{
    return cb;
}

/**
 * @brief Check if a given InfiniBand device supports fast registration.
 * 
 * This function examines the device capabilities flags of the provided InfiniBand
 * device and determines if it supports the necessary flags for fast registration.
 * 
 * @param dev Pointer to the InfiniBand device structure.
 *            Assumed to be a valid pointer.
 * 
 * @return 1 if fast registration is supported, 0 otherwise.
 * 
 * @note This function checks if the specified device has the required memory
 * management extensions for fast registration. If the necessary flags are not set
 * in the device capabilities, the function returns 0, indicating that fast
 * registration is not supported. Otherwise, it returns 1, indicating support for
 * fast registration.
 * 
 * @note The function prints debug logs indicating whether fast registration is
 * supported based on the device capabilities flags.
 */
static int reg_supported(struct ib_device *dev)
{
	u64 needed_flags = IB_DEVICE_MEM_MGT_EXTENSIONS;

	if ((dev->attrs.device_cap_flags & needed_flags) != needed_flags) {
		DEBUG_LOG(CL "Fastreg not supported - device_cap_flags 0x%llx\n", (unsigned long long)dev->attrs.device_cap_flags);
		return 0;
	}
	DEBUG_LOG(CL "Fastreg supported - device_cap_flags 0x%llx\n", (unsigned long long)dev->attrs.device_cap_flags);
	return 1;
}

/**
 * @brief Handle RDMA connection management events.
 * 
 * This function is an event handler for RDMA connection management events. It
 * processes various events related to the establishment and teardown of RDMA
 * connections, updating the state of the associated control block (cb) accordingly.
 * 
 * @param cma_id Pointer to the RDMA connection identifier structure.
 *               Assumed to be a valid pointer.
 * 
 * @param event Pointer to the RDMA connection management event structure.
 *              Assumed to be a valid pointer.
 * 
 * @return 0 on successful event processing.
 * 
 * @note This function handles different RDMA connection management events and
 * updates the state of the control block (cb) based on the received events. It
 * also logs debug information related to the event type, the associated connection
 * number, and the state transitions.
 * 
 * @note The function switches on the event type and performs specific actions
 * for each event, such as resolving addresses, routes, handling connect requests,
 * and managing error and disconnection events.
 * 
 * @note The control block (cb) is assumed to be a global variable accessible from
 * within the function.
 * 
 * @note The function uses wake_up_interruptible(&cb->sem) to wake up any processes
 * waiting on the semaphore associated with the control block, allowing them to
 * proceed after handling the event.
 */
static int rkit_cma_event_handler(struct rdma_cm_id *cma_id, struct rdma_cm_event *event)
{
    int ret;
    cb = cma_id->context;
    DEBUG_LOG(CL "Inside cma_event_handler function\n");
    DEBUG_LOG(CL "cma_event type %d cma_id %p\n", event->event, cma_id);
    DEBUG_LOG(CL "cb->state: %d\n", cb->state[connection_number]);
    
    switch (event->event) {
        case RDMA_CM_EVENT_ADDR_RESOLVED:
            DEBUG_LOG(CL "CLIENT CMA: state changed from %d to %d\n", cb->state[connection_number], ADDR_RESOLVED);
            DEBUG_LOG(CL "RDMA_CM_EVENT_ADDR_RESOLVED");
            cb->state[connection_number] = ADDR_RESOLVED;
            DEBUG_LOG(CL "rdma_resolve_route(cma_id, 2000)\n");
            ret = rdma_resolve_route(cma_id, 2000);
            if (ret) {
                pr_err(CL "rdma_resolve_route error %d\n", ret);
                wake_up_interruptible(&cb->sem);
            }
            break;
        case RDMA_CM_EVENT_ROUTE_RESOLVED:
            DEBUG_LOG(CL "CLIENT CMA: state changed from %d to %d\n", cb->state[connection_number], ROUTE_RESOLVED);
            DEBUG_LOG(CL "RDMA_CM_EVENT_ROUTE_RESOLVED");
            cb->state[connection_number] = ROUTE_RESOLVED;
            wake_up_interruptible(&cb->sem);
            break;
        case RDMA_CM_EVENT_CONNECT_REQUEST:
            DEBUG_LOG(CL "CLIENT CMA: state changed from %d to %d\n", cb->state[connection_number], CONNECT_REQUEST);
            DEBUG_LOG(CL "RDMA_CM_EVENT_CONNECT_REQUEST");
            cb->state[connection_number] = CONNECT_REQUEST;
            wake_up_interruptible(&cb->sem);
            break;
        case RDMA_CM_EVENT_ESTABLISHED:
            DEBUG_LOG(CL "CLIENT CMA: state changed from %d to %d\n", cb->state[connection_number], CONNECTED);
            DEBUG_LOG(CL "ESTABLISHED\n");
            cb->state[connection_number] = CONNECTED;
            wake_up_interruptible(&cb->sem);
            break;
        case RDMA_CM_EVENT_ADDR_ERROR:
        case RDMA_CM_EVENT_ROUTE_ERROR:
        case RDMA_CM_EVENT_CONNECT_ERROR:
        case RDMA_CM_EVENT_UNREACHABLE:
        case RDMA_CM_EVENT_REJECTED:
            pr_err( CL "cma event %d, error %d\n", event->event, event->status);
            DEBUG_LOG(CL "CLIENT CMA: state changed from %d to %d\n", cb->state[connection_number], ERROR);
            DEBUG_LOG(CL "change state to error\n");
            cb->state[connection_number] = ERROR;
            DEBUG_LOG(CL "before wakeup\n");
            wake_up_interruptible(&cb->sem);
            DEBUG_LOG(CL "after wakeup\n");
            break;
        case RDMA_CM_EVENT_DISCONNECTED:
            DEBUG_LOG(CL "CLIENT CMA: state changed from %d to %d\n", cb->state[connection_number], ERROR);
            pr_err( CL "DISCONNECT EVENT...\n");
            cb->state[connection_number] = ERROR;
            wake_up_interruptible(&cb->sem);
            break;
        case RDMA_CM_EVENT_DEVICE_REMOVAL:
            DEBUG_LOG(CL "CLIENT CMA: state changed from %d to %d\n", cb->state[connection_number], ERROR);
            pr_err(CL "cma detected device removal!!!!\n");
            cb->state[connection_number] = ERROR;
            wake_up_interruptible(&cb->sem);
            break;
        default:
            DEBUG_LOG(CL"CLIENT CMA: state changed from %d to default error\n", cb->state[connection_number]);
            pr_err(CL "of bad type!\n");
            wake_up_interruptible(&cb->sem);
            break;
    }

    return 0;
}

/**
 * @brief Initiate a client-side connection using RDMA.
 * 
 * This function initiates a connection to the server using RDMA. It sets up the
 * connection parameters and performs the connection using rdma_connect. The function
 * then waits for the connection to be established or encounters an error by waiting
 * on the control block's semaphore.
 * 
 * @param cm_id Pointer to the RDMA connection identifier structure.
 *              Assumed to be a valid pointer.
 * 
 * @return 0 on successful connection establishment, a non-zero value otherwise.
 * 
 * @note This function is intended for client-side connection initiation in an RDMA
 * communication setup. It sets up the connection parameters such as responder
 * resources, initiator depth, and retry count.
 * 
 * @note The function logs debug information, including the initiation of the
 * connection using rdma_connect and any resulting errors.
 * 
 * @note The function uses the wait_event_interruptible macro to wait for the
 * connection state to transition to CONNECTED or encounter an ERROR. It checks the
 * state using the control block's semaphore and returns an error if the state
 * transition is not successful.
 * 
 * @note The control block (cb) is assumed to be a global variable accessible from
 * within the function.
 */
static int rkit_connect_client(struct rdma_cm_id *cm_id)
{
    struct rdma_conn_param conn_param;
    int ret;

    memset(&conn_param, 0, sizeof conn_param);
    conn_param.responder_resources = 1;
    conn_param.initiator_depth = 1;
    conn_param.retry_count = 7;
    conn_param.rnr_retry_count = 7;

    DEBUG_LOG(CL "Requesting connection using rdma_connect\n");    
    ret = rdma_connect(cm_id, &conn_param);
    if (ret)
    {
        pr_err(CL "rdma_connect error %d\n", ret);
        return ret;
    }

    wait_event_interruptible(cb->sem, cb->state[connection_number] >= CONNECTED);
    if (cb->state[connection_number] == ERROR)
    {
        DEBUG_LOG(CL "wait for CONNECTED state %d\n", cb->state[connection_number]);
        return -1;
    }

    return 0;
}

/**
 * @brief Bind a client to an RDMA server.
 * 
 * This function binds a client to an RDMA server by resolving the address and
 * route for the given RDMA connection identifier (cm_id). It fills the socket
 * address structure, resolves the address using rdma_resolve_addr, waits for the
 * resolution to be completed, checks if route resolution is successful, and
 * verifies if fast registration is supported on the device.
 * 
 * @param cm_id Pointer to the RDMA connection identifier structure.
 *              Assumed to be a valid pointer.
 * 
 * @return 0 on successful binding, a non-zero value otherwise.
 * 
 * @note This function is intended for binding a client to an RDMA server and
 * preparing the connection for communication.
 * 
 * @note The function logs debug information, including the filling of the socket
 * address structure, resolving the address using rdma_resolve_addr, and checking
 * the result of the address and route resolution.
 * 
 * @note The function uses the wait_event_interruptible macro to wait for the
 * connection state to transition to ROUTE_RESOLVED or encounter an error. It
 * returns an error if the resolution is not successful.
 * 
 * @note The function checks if fast registration is supported on the device using
 * the reg_supported function, and returns an error if it is not supported.
 * 
 * @note The control block (cb) is assumed to be a global variable accessible from
 * within the function.
 */
static int rkit_bind_client(struct rdma_cm_id *cm_id)
{
    struct sockaddr_storage sin;
    int ret = 0;

    fill_sockaddr(&sin);
    DEBUG_LOG(CL "socket address filled\n");
    DEBUG_LOG(CL "cb->cm_id = %p\n", cm_id);
    ret = rdma_resolve_addr(cm_id, NULL, (struct sockaddr *)&sin, 2000);
    if (ret)
    {
        pr_err( CL "rdma_resolve_addr error %d\n", ret);
        return ret;
    }
    wait_event_interruptible(cb->sem, cb->state[connection_number] >= ROUTE_RESOLVED);
	if (cb->state[connection_number] != ROUTE_RESOLVED) {
		pr_err("addr/route resolution did not resolve: state %d\n", cb->state[connection_number]);
		return -EINTR;
	}
    DEBUG_LOG(CL "Address got resolved successfully\n");

    if (!reg_supported(cm_id->device))
		return -EINVAL;
    
    return ret;
}

/**
 * @brief Create an RDMA Queue Pair (QP) for communication.
 * 
 * This function creates an RDMA Queue Pair (QP) for communication using the provided
 * RDMA connection identifier (cm_id). It sets up the necessary attributes for the QP,
 * including the maximum number of send and receive work requests, inline data size,
 * and other parameters.
 * 
 * @param cm_id Pointer to the RDMA connection identifier structure.
 *              Assumed to be a valid pointer.
 * 
 * @return 0 on successful QP creation, a non-zero value otherwise.
 * 
 * @note This function assumes the existence of a control block (cb) with the necessary
 * information, including the protection domain (pd), completion queues (cq), and QP type.
 * 
 * @note The function logs debug information, including the maximum number of send and
 * receive work requests, maximum inline data size, and the signaling type for the QP.
 * 
 * @note The QP creation is attempted using rdma_create_qp, and if successful, the QP
 * associated with the control block is updated.
 */
static int rkit_create_qp(struct rdma_cm_id *cm_id)
{
    struct ib_qp_init_attr init_attr;
    int ret;

    memset(&init_attr, 0, sizeof(init_attr));
    init_attr.cap.max_send_wr = maximum_send_wr_size;
    init_attr.cap.max_recv_wr = maximum_recv_wr_size;
    init_attr.cap.max_inline_data =  maximum_inline_size;

    /* For flush_qp() */
    init_attr.cap.max_send_wr++;
    init_attr.cap.max_recv_wr++;

    init_attr.cap.max_recv_sge = 1;
    init_attr.cap.max_send_sge = 1;
    init_attr.qp_type = RKIT_QP_MODE;
    init_attr.send_cq = cb->cq[connection_number];
    init_attr.recv_cq = cb->cq[connection_number];
    init_attr.sq_sig_type = IB_SIGNAL_REQ_WR;
    ret = rdma_create_qp(cm_id, cb->pd, &init_attr);
    if (!ret)
        cb->qp[connection_number] = cm_id->qp;

    return ret;
}

static void rkit_setup_wr(void)
{
    // setup recieve work request
    // this work request is used to get rkit_data from servet
    DEBUG_LOG(CL "setup the recieve work request\n");    
    cb->recv_sge.addr = cb->recv_dma_addr;
    cb->recv_sge.length = sizeof cb->recv_buf;
    cb->recv_sge.lkey = cb->pd->local_dma_lkey;
    cb->recv_wr.sg_list = &cb->recv_sge;
    cb->recv_wr.num_sge = 1;
    cb->recv_wr.wr_id = 0;
}

static int rkit_setup_buffers(void)
{
    DEBUG_LOG(CL "map the recieve buffer to get dma address\n");
    cb->recv_dma_addr = ib_dma_map_single(cb->pd->device, &cb->recv_buf, sizeof(cb->recv_buf), DMA_BIDIRECTIONAL);
    rkit_setup_wr();
    DEBUG_LOG(CL "allocated & registered buffers...\n");
    return 0;
}

/**
 * @brief Setup the Queue Pair (QP) and associated resources for RDMA communication.
 * 
 * This function sets up the Queue Pair (QP) and the associated resources, including
 * the protection domain (PD) and completion queue (CQ), for RDMA communication.
 * 
 * @param cm_id Pointer to the RDMA connection identifier structure.
 *              Assumed to be a valid pointer.
 * 
 * @return 0 on successful QP setup, a non-zero value otherwise.
 * 
 * @note This function assumes the existence of a control block (cb) with the necessary
 * information, including the protection domain (pd), completion queue (cq), and QP.
 * 
 * @note The function uses preprocessor directives to conditionally create the protection
 * domain only once for the first connection (connection_number == 0).
 * 
 * @note The function logs debug information, including the created protection domain,
 * completion queue, and QP.
 * 
 * @note The function calls rkit_create_qp to create the QP and checks for errors in each
 * step of the setup process, deallocating resources if an error occurs.
 */
static int rkit_setup_qp(struct rdma_cm_id *cm_id)
{
    int ret;
    struct ib_cq_init_attr attr = {0};

    if (connection_number == 0){
        // create protection domain
        cb->pd = ib_alloc_pd(cm_id->device, 0); // create one shared protection domain for all connections.
        if (IS_ERR(cb->pd))
        {
            pr_err(CL "ib_alloc_pd failed\n");
            return PTR_ERR(cb->pd);
        } 
        
    }   
    DEBUG_LOG(CL "created pd %p\n", cb->pd);

    attr.cqe = maximum_cqe_size;
    attr.comp_vector = 0;
    cb->cq[connection_number] = ib_create_cq(cm_id->device, NULL, NULL, NULL, &attr);
    if (IS_ERR(cb->cq[connection_number]))
    {
        pr_err(CL "ib_create_cq failed\n");
        ret = PTR_ERR(cb->cq[connection_number]);
        ib_dealloc_pd(cb->pd);
        return ret;
    }
    DEBUG_LOG(CL "created cq %p\n", cb->cq[connection_number]);

    ret = rkit_create_qp(cm_id);
    if (ret)
    {
        pr_err(CL "rkit_create_qp failed: %d\n", ret);
        ib_destroy_cq(cb->cq[connection_number]);
        ib_dealloc_pd(cb->pd);
        return ret;
    }
    DEBUG_LOG(CL "created qp %p\n", cb->qp[connection_number]);

    return ret;
}

int rkit_init_client(const char * addr, const uint16_t port)
{
    int ret = 0;   
    int i;
    struct ib_wc wc;
    #if ENABLE_INFINTE_LOOP_PROTECTION == 1
    int max_try = 0;
    #endif

    // init cb
    cb               = kmalloc(sizeof(struct rkit_client_handler), GFP_KERNEL);
    cb->addr_str     = kmalloc(strlen(addr), GFP_KERNEL);
    cb->state        = kmalloc(number_of_connection_client*sizeof(enum rkit_state),GFP_KERNEL);
    cb->cq           = kmalloc(number_of_connection_client*sizeof(struct ib_cq*),GFP_KERNEL);
    cb->qp           = kmalloc(number_of_connection_client*sizeof(struct ib_qp*),GFP_KERNEL);
    cb->cm_id        = kmalloc(number_of_connection_client*sizeof(struct rdma_cm_id*),GFP_KERNEL);
    cb->remote_rkeys = kmalloc(number_of_connection_client*sizeof(uint32_t),GFP_KERNEL);
    cb->remote_addrs = kmalloc(number_of_connection_client*sizeof(uint64_t),GFP_KERNEL);
    cb->remote_lens  = kmalloc(number_of_connection_client*sizeof(uint32_t),GFP_KERNEL);

    strcpy(cb->addr_str, addr);
    in4_pton(addr, -1, cb->addr, -1, NULL);
    cb->addr_type = AF_INET;
    cb->port = port;
    for(i = 0; i < number_of_connection_client;i++)
        cb->state[i] = IDLE;
    init_waitqueue_head(&cb->sem);

    for(connection_number = 0; connection_number < number_of_connection_client; connection_number++)
    {
        // init rdma id
        cb->cm_id[connection_number] = rdma_create_id(&init_net, rkit_cma_event_handler, cb, RDMA_PS_TCP, RKIT_QP_MODE);
        if (IS_ERR(cb->cm_id[connection_number]))
        {
            ret = PTR_ERR(cb->cm_id[connection_number]);
            pr_err("rdma_create_id error %d\n", ret);
            kfree(cb);
            return ret;
        }
        DEBUG_LOG(CL "created cm_id %p\n", cb->cm_id[connection_number]);

        ret = rkit_bind_client(cb->cm_id[connection_number]);
        if (ret){
            pr_err(CL "Failed to bind client: %d\n",ret);
            return ret;
        }
        DEBUG_LOG(CL "Client binded successfully\n");

        ret = rkit_setup_qp(cb->cm_id[connection_number]);
        if (ret)
        {
            pr_err(CL "setup_qp failed: %d\n", ret);
            return ret;
        }
        DEBUG_LOG(CL "Queue pair got setuped successfully\n");

        if (connection_number == 0) // we do buffer setup once before the first connection is done
        {

                // setup buffers
                ret = rkit_setup_buffers();
                if (ret)
                {
                    pr_err("setup_buffers failed: %d\n", ret);
                    rkit_free_qp();
                    return ret;
                }
                DEBUG_LOG(CL "Buffers got setuped successfully\n");

        } 
        // the rkit_data from server is recived from this posted recieve work request
        ret = ib_post_recv(cb->qp[connection_number], &cb->recv_wr, NULL);
        if(ret)
        {
            pr_err(CL "ib_post_recv failed: %d\n",ret);
            return ret;
        }
        
        ret = rkit_connect_client(cb->cm_id[connection_number]);
        if (ret)
        {
            pr_err(CL "connecttion error %d\n", ret);
            rkit_free_buffers();
            rkit_free_qp();
            return ret;
        }
        DEBUG_LOG(CL "Client successfully connected to port=%d addr=%s connection number=%d\n",cb->port, cb->addr_str, connection_number);
    }
    
    for(i = 0; i < number_of_connection_client;i++){
        ret = 0;
        #if ENABLE_INFINTE_LOOP_PROTECTION == 1
        max_try = 0;
        #endif
        while(!ret)
        {
            ret = ib_poll_cq(cb->cq[i],1,&wc);
            if(ret)
            {
                if(wc.status){
                    printk(CL "status=%x\n",wc.status);
                }
            }
            #if ENABLE_INFINTE_LOOP_PROTECTION == 1
            max_try += 1;
            if (max_try >= RKIT_MAX_TRY_LOOP){
                pr_err(CL "maximum tries reached in the loop, Interrupted!\n");
                return EINTR;
            }
            #endif
        }
        cb->remote_rkeys[i] = ntohl(cb->recv_buf.rkey);       // Convert the received rkey to host byte order and store it in the context
        cb->remote_addrs[i] = ntohll(cb->recv_buf.buf);       // Convert the received buffer address to host byte order and store it in the context
        DEBUG_LOG(CL "Received rkey %x addr %llx from peer\n", 
        cb->remote_rkeys[i], 
        (unsigned long long) cb->remote_addrs[i]);
    }

    // below two semaphores are used to sync client and server
    down_interruptible(&server_client_sync1);
    up(&server_client_sync2);
    return 0;
}

struct ib_send_wr *rkit_create_send_batch(struct rkit_work_request * wrs)
{    
    int i = -1;
    struct ib_send_wr* return_wr;
    struct ib_send_wr* prev_wr;
    do
    {
        struct ib_send_wr* wr;
        struct ib_rdma_wr *rdma_wr;
        struct ib_sge *sge = kzalloc(sizeof(struct ib_sge),GFP_KERNEL);
        sge->length = wrs->length;
        sge->lkey = cb->pd->local_dma_lkey;
        i = i + 1;

        switch (wrs->operation)
        {
            case WRITE_OP:
                rdma_wr = kzalloc(sizeof(struct ib_rdma_wr),GFP_KERNEL);
                rdma_wr->wr.sg_list = sge;
                rdma_wr->wr.num_sge = 1;
                rdma_wr->remote_addr = cb->remote_addrs[0] + wrs->remote_addr;
                rdma_wr->rkey = cb->remote_rkeys[0];
                rdma_wr->wr.opcode = IB_WR_RDMA_WRITE;
                if (wrs->length < maximum_inline_size)
                {
                    rdma_wr->wr.send_flags = IB_SEND_INLINE; 
                    sge->addr = wrs->virtual_address;
                }
                else
                {
                    sge->addr = ib_dma_map_single(cb->pd->device, (void *)wrs->virtual_address, wrs->length, DMA_BIDIRECTIONAL);
                }
                wr = &rdma_wr->wr;
                break;

            case READ_OP:
                rdma_wr = kzalloc(sizeof(struct ib_rdma_wr),GFP_KERNEL);
                rdma_wr->wr.sg_list = sge;
                rdma_wr->wr.num_sge = 1;
                rdma_wr->remote_addr = cb->remote_addrs[0] + wrs->remote_addr;
                rdma_wr->rkey = cb->remote_rkeys[0];
                rdma_wr->wr.opcode = IB_WR_RDMA_READ;
                sge->addr = ib_dma_map_single(cb->pd->device, (void *)wrs->virtual_address, wrs->length, DMA_BIDIRECTIONAL);
                wr = &rdma_wr->wr;
                break;

            case SEND_OP:
                wr = kzalloc(sizeof(struct ib_send_wr),GFP_KERNEL);
                wr->opcode = IB_WR_SEND;
                wr->sg_list = sge;
                wr->num_sge = 1;
                if (0 < wrs->length && wrs->length < maximum_inline_size)
                {
                   sge->addr = wrs->virtual_address;
                   wr->send_flags = IB_SEND_INLINE; 
                }
                else
                {
                   sge->addr = ib_dma_map_single(cb->pd->device, (void *)wrs->virtual_address, wrs->length, DMA_BIDIRECTIONAL);
                }
                break; 
 
            case WRITE_IMM_OP:
                rdma_wr = kzalloc(sizeof(struct ib_rdma_wr),GFP_KERNEL);
                rdma_wr->wr.sg_list = sge;
                rdma_wr->wr.num_sge = 1;
                rdma_wr->remote_addr = cb->remote_addrs[0] + wrs->remote_addr;
                rdma_wr->rkey = cb->remote_rkeys[0];
                rdma_wr->wr.opcode = IB_WR_RDMA_WRITE_WITH_IMM;
                if (0 < wrs->length && wrs->length < maximum_inline_size)
                {
                    rdma_wr->wr.send_flags = IB_SEND_INLINE; 
                    sge->addr = wrs->virtual_address;
                }
                else
                {
                    sge->addr = ib_dma_map_single(cb->pd->device, (void *)wrs->virtual_address, wrs->length, DMA_BIDIRECTIONAL);
                }
                wr = &rdma_wr->wr;   
                rdma_wr->wr.ex.imm_data = wrs->imm_data;  
                break;   
    
            case READ_WITH_NOTIFICATION_OP:
                rdma_wr = kzalloc(sizeof(struct ib_rdma_wr),GFP_KERNEL);
                rdma_wr->wr.sg_list = sge;
                rdma_wr->wr.num_sge = 1;
                rdma_wr->remote_addr = cb->remote_addrs[0] + wrs->remote_addr;
                rdma_wr->rkey = cb->remote_rkeys[0];
                rdma_wr->wr.opcode = IB_WR_RDMA_READ;
                sge->addr = ib_dma_map_single(cb->pd->device, (void *)wrs->virtual_address, wrs->length, DMA_BIDIRECTIONAL);
                struct ib_rdma_wr *imm_rdma_wr = kzalloc(sizeof(struct ib_rdma_wr),GFP_KERNEL);
                struct ib_sge *imm_sge = kzalloc(sizeof(struct ib_sge),GFP_KERNEL);
                imm_sge->length = 0;
                imm_rdma_wr->wr.sg_list = imm_sge;
                imm_rdma_wr->wr.num_sge = 1;
                imm_rdma_wr->remote_addr = cb->remote_addrs[0] + wrs->remote_addr;
                imm_rdma_wr->rkey = cb->remote_rkeys[0];
                imm_rdma_wr->wr.opcode = IB_WR_RDMA_WRITE_WITH_IMM;
                imm_rdma_wr->wr.send_flags = IB_SEND_FENCE;
                imm_rdma_wr->wr.ex.imm_data = wrs->imm_data;
                rdma_wr->wr.next = &imm_rdma_wr->wr;
                wr = &rdma_wr->wr;   
                break;
            case SEND_IMM_OP:
                wr = kzalloc(sizeof(struct ib_send_wr),GFP_KERNEL);
                wr->opcode = IB_WR_SEND_WITH_IMM;
                wr->sg_list = sge;
                wr->num_sge = 1;
                wr->ex.imm_data = wrs->imm_data;
                if (0 < wrs->length && wrs->length < maximum_inline_size)
                {
                   sge->addr = wrs->virtual_address;
                   wr->send_flags = IB_SEND_INLINE; 
                }
                else
                {
                   sge->addr = ib_dma_map_single(cb->pd->device, (void *)wrs->virtual_address, wrs->length, DMA_BIDIRECTIONAL);
                }
                break;    

        }
        
        if (i == 0)
        {
            return_wr = wr;
        }
        else
        {
            prev_wr->next = wr;
        }
        prev_wr = wr;
        if (!wrs->next){
            wr->send_flags |= IB_SEND_SIGNALED;
        }
        wrs = wrs->next;
    } while(wrs);

    return return_wr;
}

/**
 * @brief Initiate an RDMA transfer using the specified Queue Pair (QP).
 * 
 * This function initiates an RDMA transfer using the specified Queue Pair (QP).
 * The transfer mode and operation type are determined by the configuration parameters
 * RKIT_TRANSFER_MODE and RKIT_ENABLE_BLOCKING_SEND.
 * 
 * @param qp_number The identifier of the Queue Pair (QP) to use for the transfer.
 *                  Assumed to be a valid QP number.
 * 
 * @return 0 on successful initiation of the transfer, a non-zero value otherwise.
 * 
 * @note This function assumes the existence of a control block (cb) with the necessary
 * information, including the Queue Pair (QP), send and RDMA write work requests (send_wrs
 * and rdma_wr), and completion queue (cq).
 * 
 * @note The function uses preprocessor directives to conditionally post a send work request
 * for two-sided operations (RKIT_TRANSFER_MODE == 0) or an RDMA write work request for
 * one-sided operations (RKIT_TRANSFER_MODE >= 1).
 * 
 * @note If RKIT_ENABLE_BLOCKING_SEND is enabled, the function polls the completion queue
 * for work completions and logs any errors.
 * 
 * @note The function returns 0 on success and logs any post send errors or completion
 * errors when RKIT_ENABLE_BLOCKING_SEND is enabled.
 */
int rkit_transfer(uint8_t thread_number, struct ib_send_wr* wr, uint8_t wait_for_wc)
{
    int ret = 0;
    #if ENABLE_INFINTE_LOOP_PROTECTION == 1
    int max_try = 0;
    #endif
    struct ib_wc wc;
    ret = ib_post_send(cb->qp[0],wr,NULL);
    if (unlikely(ret))
    {
        pr_err(CL "post send error %d\n",ret);
        return ret;
    }
    if(wait_for_wc){
        while (!ret){
            ret = ib_poll_cq(cb->cq[0], 1, &wc);
            if(ret){
                if (wc.status)
                {  
                  if (wc.status == IB_WC_WR_FLUSH_ERR) {  // Check if the event was a WR flush error
				    pr_err(CL "CLIENT cq flushed in handler\n");  // Log that the CQ was flushed
				    return -1;
			    } 
                else
                {
			    	pr_err(CL "CLIENT cq completion failed with "
				       "wr_id %Lx status %d opcode %d vender_err %x\n",
				    	wc.wr_id, wc.status, wc.opcode, wc.vendor_err);  // Log the error message  
                        return -1;
                }
                }
        }
        #if ENABLE_INFINTE_LOOP_PROTECTION == 1
        max_try += 1;
        if (max_try >= RKIT_MAX_TRY_LOOP)
        {
            pr_err(CL "max try reached in rkit_transfer function\n");
            return -1;
        }
        #endif
        }
    }

    return 0;
}

struct rkit_client_handler * rkit_get_client_handler(void){
    return cb;
}