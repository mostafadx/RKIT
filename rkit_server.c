#include "rkit_server.h"

static struct rkit_server_handler *cb;
static int connection_number;

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
 */
static int rkit_cma_event_handler(struct rdma_cm_id *cma_id, struct rdma_cm_event *event)
{
    int ret = 0;
    cb = cma_id->context;

    DEBUG_LOG(SR "cma_event type %d cma_id %p connection number=%d\n", event->event, cma_id, connection_number);
    DEBUG_LOG(SR "cb->state: %d", cb->state[connection_number]);

    switch (event->event)
    {
        case RDMA_CM_EVENT_ADDR_RESOLVED:
            DEBUG_LOG(SR "SERVER CMA: state changed from %d to %d\n", cb->state[connection_number], RDMA_CM_EVENT_ADDR_RESOLVED);
            DEBUG_LOG(SR "RDMA_CM_EVENT_ADDR_RESOLVED");
            cb->state[connection_number] = ADDR_RESOLVED;
            ret = rdma_resolve_route(cma_id, 2000);
            if (ret) 
            {
                pr_err( SR "rdma_resolve_route error %d\n", ret);
                wake_up_interruptible(&cb->sem);
            }
            break;

        case RDMA_CM_EVENT_ROUTE_RESOLVED:
            DEBUG_LOG(SR "SERVER CMA: state changed from %d to %d\n", cb->state[connection_number], RDMA_CM_EVENT_ROUTE_RESOLVED);
            DEBUG_LOG(SR "RDMA_CM_EVENT_ROUTE_RESOLVED");
            cb->state[connection_number] = ROUTE_RESOLVED;
            wake_up_interruptible(&cb->sem);
            break;

        case RDMA_CM_EVENT_CONNECT_REQUEST:
            DEBUG_LOG(SR "SERVER CMA: state changed from %d to %d\n", cb->state[connection_number], CONNECT_REQUEST);
            DEBUG_LOG(SR "RDMA_CM_EVENT_CONNECT_REQUEST");
            cb->state[connection_number] = CONNECT_REQUEST;
            cb->child_cm_id[connection_number] = cma_id;
            DEBUG_LOG(SR "child cma %p\n", cb->child_cm_id[connection_number]);
            wake_up_interruptible(&cb->sem);
            break;

        case RDMA_CM_EVENT_ESTABLISHED:
            DEBUG_LOG(SR "SERVER CMA: state changed from %d to %d\n", cb->state[connection_number], CONNECTED);
            cb->state[connection_number] = CONNECTED;
            DEBUG_LOG(SR "ESTABLISHED\n");
            wake_up_interruptible(&cb->sem);
            break;

        case RDMA_CM_EVENT_ADDR_ERROR:
        case RDMA_CM_EVENT_ROUTE_ERROR:
        case RDMA_CM_EVENT_CONNECT_ERROR:
        case RDMA_CM_EVENT_UNREACHABLE:
        case RDMA_CM_EVENT_REJECTED:
            pr_err(SR "cma event %d, error %d\n", event->event, event->status);
            DEBUG_LOG(SR "SERVER CMA: state changed from %d to %d\n", cb->state[connection_number], ERROR);
            DEBUG_LOG(SR "change state to error\n");
            cb->state[connection_number] = ERROR;
            DEBUG_LOG(SR "before wakeup\n");
            wake_up_interruptible(&cb->sem);
            DEBUG_LOG(SR "after wakeup\n");
            break;

        case RDMA_CM_EVENT_DISCONNECTED:
            DEBUG_LOG(SR "SERVER CMA: state changed from %d to %d\n", cb->state[connection_number], ERROR);
            pr_err(SR "DISCONNECT EVENT...\n");
            cb->state[connection_number] = ERROR;
            wake_up_interruptible(&cb->sem);
            break;

        case RDMA_CM_EVENT_DEVICE_REMOVAL:
            DEBUG_LOG(SR "SERVER CMA: state changed from %d to %d\n", cb->state[connection_number], ERROR);
            pr_err(SR "cma detected device removal!!!!\n");
            cb->state[connection_number] = ERROR;
            wake_up_interruptible(&cb->sem);
            break;

        default:
            DEBUG_LOG(SR "SERVER CMA: state changed from %d to default error\n", cb->state[connection_number]);
            pr_err(SR "oof bad type!\n");
            wake_up_interruptible(&cb->sem);
            break;
    }

    return ret;
}

/**
* @brief Fill sockaddr structure with address information.
*
* @param sin Pointer to a sockaddr_storage structure to be filled.
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
 * @brief Bind and listen for incoming connections on the server.
 * 
 * This function binds the server's RDMA connection identifier (cm_id) to a specified
 * socket address and initiates listening for incoming connections. The socket address
 * is filled using the fill_sockaddr function.
 * 
 * @return 0 on successful bind and listen, a non-zero value otherwise.
 */
static int rkit_bind_server(void)
{
    struct sockaddr_storage sin;
    int ret = 0;

    fill_sockaddr(&sin);

    ret = rdma_bind_addr(cb->cm_id[connection_number], (struct sockaddr *)&sin);
    if (ret) {
		pr_err(SR "rdma_bind_addr error %d\n", ret);
		return ret;
	}
	DEBUG_LOG(SR "rdma_bind_addr successful\n");

	ret = rdma_listen(cb->cm_id[connection_number], 3);
	if (ret) {
		pr_err(SR "rdma_listen failed: %d\n", ret);
		return ret;
	}

	DEBUG_LOG(SR "Server is listening on port=%d and address=%s\n",cb->port,cb->addr_str);
    return ret;
}

/**
 * @brief Prepare the server for RDMA communication.
 * 
 * This function initializes the server's control block (cb) object and sets up
 * the necessary parameters for RDMA communication, including the server's address,
 * port, and connection identifiers (cm_id) for multiple connections. It also binds
 * the server to the specified address and port.
 * 
 * @param addr The string representation of the server's IP address.
 *             Assumed to be a valid null-terminated string.
 * @param port The port number on which the server will listen for incoming connections.
 *             Assumed to be a valid port number.
 * 
 * @return 0 on successful preparation for communication, a non-zero value otherwise.
 */
static int rkit_pre_start(const char *addr,const uint16_t port){
    int ret = 0;
    int i;
    // init cb object
    cb                 = kmalloc(sizeof(struct rkit_server_handler), GFP_KERNEL);
    cb->addr_str       = kmalloc(strlen(addr), GFP_KERNEL);
    cb->state          = kmalloc(number_of_connection_server*sizeof(enum rkit_state),GFP_KERNEL);
    cb->cq             = kmalloc(number_of_connection_server*sizeof(struct ib_cq*),GFP_KERNEL);
    cb->qp             = kmalloc(number_of_connection_server*sizeof(struct ib_qp*),GFP_KERNEL);
    cb->child_cm_id    = kmalloc(number_of_connection_server*sizeof(struct rdma_cm_id*),GFP_KERNEL);
    cb->cm_id          = kmalloc(number_of_connection_server*sizeof(struct rdma_cm_id*),GFP_KERNEL);

    strcpy(cb->addr_str, addr);
    in4_pton(addr, -1, cb->addr, -1,NULL);
    cb->addr_type = AF_INET;
    for(i = 0; i < number_of_connection_server;i++)
        cb->state[i] = IDLE;
    init_waitqueue_head(&cb->sem);
    cb->port = port;

    for(i = 0; i < number_of_connection_server;i++)
    {
        cb->cm_id[i] = rdma_create_id(&init_net, rkit_cma_event_handler, cb, RDMA_PS_TCP, RKIT_QP_MODE);
        if (IS_ERR(cb->cm_id[i]))
        {
            ret = PTR_ERR(cb->cm_id[i]);
            pr_err(SR "rdma_create_id error %d and connection number=%d\n", ret,connection_number);
            return ret;
        }
        DEBUG_LOG(SR "ID for connection managment was created successfully connection number=%d\n",connection_number);
    }

    ret = rkit_bind_server();
    if (ret)
    {
        return ret;
    }
    DEBUG_LOG(SR "Server binded successfully\n");

    return ret;
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
 */
static int reg_supported(struct ib_device *dev)
{
	u64 needed_flags = IB_DEVICE_MEM_MGT_EXTENSIONS;

	if ((dev->attrs.device_cap_flags & needed_flags) != needed_flags) {
		DEBUG_LOG(SR "Fastreg not supported - device_cap_flags 0x%llx\n", (unsigned long long)dev->attrs.device_cap_flags);
		return 0;
	}
    
	DEBUG_LOG(SR "Fastreg supported - device_cap_flags 0x%llx\n", (unsigned long long)dev->attrs.device_cap_flags);
	return 1;
}

/**
 * @brief Free the resources associated with RDMA Queue Pairs (QPs).
 * 
 * This function releases the resources allocated for RDMA Queue Pairs (QPs) and their
 * associated Completion Queues (CQs). It iterates through the array of QPs and CQs in the
 * control block (cb) and destroys each one. The purpose is to clean up and release
 * resources when they are no longer needed, preventing memory leaks.
 */
static void rkit_free_qp(void)
{
    int i;
    for (i = 0; i < number_of_connection_server;i++)
    {
        if(cb->cq && cb->cq[i])
            ib_destroy_cq(cb->cq[i]);
        if(cb->qp && cb->qp[i])
            ib_destroy_qp(cb->qp[i]);
    }
    DEBUG_LOG(SR "QP(s) got free.");
}
/**
 * @brief Free the memory pages, dmas, and memory region
 */
static void rkit_free_buffers(void){
    int i;
    DEBUG_LOG(SR "Free memory region...\n");
}

/**
 * @brief Creates a Queue Pair (QP) with the specified attributes for RDMA communication.
 *
 * This function is responsible for creating a Queue Pair (QP) with the specified attributes to enable
 * Remote Direct Memory Access (RDMA) communication. It performs the following tasks:
 *
 * 1. Initializes a structure for QP initialization attributes.
 * 2. Sets QP capabilities such as maximum send and receive work requests, inline data size, and QP type.
 * 3. Associates the QP with a Protection Domain (PD) and completion queues (send and receive CQ).
 * 4. Creates the QP using the specified attributes.
 *
 * @return 0 on success, or an error code on failure.
 */
static int rkit_create_qp(void)
{
    struct ib_qp_init_attr init_attr;
    int ret;
    memset(&init_attr, 0, sizeof(init_attr));
    init_attr.cap.max_send_wr = maximum_send_wr_size;
    init_attr.cap.max_recv_wr = maximum_recv_wr_size;

    /* For flush_qp() */
    init_attr.cap.max_send_wr++;
    init_attr.cap.max_recv_wr++;

    init_attr.cap.max_recv_sge = 1;
    init_attr.cap.max_send_sge = 1;

    // packet below maximum_inline_size size will be sent inline
    init_attr.cap.max_inline_data = maximum_inline_size;

    // RC (reliable connection)
    // guarantee in order and without corruption packet transfering
    init_attr.qp_type = RKIT_QP_MODE;
    init_attr.send_cq = cb->cq[connection_number];
    init_attr.recv_cq = cb->cq[connection_number];

    ret = rdma_create_qp(cb->child_cm_id[connection_number], cb->pd, &init_attr);
    if (!ret)
        cb->qp[connection_number] = cb->child_cm_id[connection_number]->qp;
    else
    {
        pr_err(SR "error rdma_create_qp ret=%d",ret);
        return ret;
    }
    
    return 0;
}

/**
 * @brief Sets up a Queue Pair (QP) and associated resources for RDMA communication.
 *
 * This function is responsible for configuring a Queue Pair (QP) and the associated resources required for
 * Remote Direct Memory Access (RDMA) communication. It performs the following tasks:
 *
 * 1. Creates a Protection Domain (PD) for memory access and registers it with the device.
 * 2. Creates a Completion Queue (CQ) for tracking completion events.
 * 3. Initializes a Queue Pair (QP) using the configured PD and CQ.
 *
 * @param cm_id A pointer to the RDMA connection manager identifier. It provides information about the
 *              RDMA device and is used for resource allocation and configuration.
 *
 * @return 0 on success, or an error code on failure.
 */
static int rkit_setup_qp(struct rdma_cm_id *cm_id){
    int ret = 0;
    struct ib_cq_init_attr attr = {0};
    // create protection domain
    if (connection_number == 0)
        cb->pd = ib_alloc_pd(cm_id->device, 0); // create one shared protection domain for all connections.

    if (IS_ERR(cb->pd))
    {
        pr_err(SR "ib_alloc_pd failed\n");
        return PTR_ERR(cb->pd);
    }
    DEBUG_LOG(SR "created pd %p\n", cb->pd);
    // create completion queue
    attr.cqe = maximum_cqe_size;
    attr.comp_vector = 0;
    cb->cq[connection_number] = ib_create_cq(cm_id->device, NULL, NULL, NULL, &attr);
    if (IS_ERR(cb->cq[connection_number]))
    {
        pr_err(SR "ib_create_cq failed\n");
        ret = PTR_ERR(cb->cq[connection_number]);
        ib_dealloc_pd(cb->pd);
        return ret;
    }
    DEBUG_LOG(SR "created cq[%d] %p\n",connection_number ,cb->cq[connection_number]);

    // create queue pair
    ret = rkit_create_qp();
    if (ret)
    {
        pr_err(SR "rkit_create_qp failed: %d\n", ret);
        ib_destroy_cq(cb->cq[0]);
        ib_dealloc_pd(cb->pd);
        return ret;
    }

    return ret;
}

/**
 * @brief Creates a Memory Region (MR) and sets up remote memory access for RDMA communication.
 *
 * This function is responsible for creating a Memory Region (MR) and configuring remote memory access
 * settings for Remote Direct Memory Access (RDMA) communication. It performs the following tasks:
 *
 * 1. Initializes a scatterlist and allocates memory for it.
 * 2. Allocates the memory region and associates it with the Protection Domain (PD).
 * 3. Configures the MR with appropriate settings, including access mode and key.
 * 4. Maps memory pages to scatterlist and associates them with the MR.
 * 5. Retrieves and returns the generated rkey for remote memory access.
 *
 * @return The generated rkey (remote access key) on success, or an error code on failure.
 */
static u32 rkit_create_mr(void)
{
    u32 rkey; 
    int ret;

    cb->reg_mr = cb->pd->device->ops.get_dma_mr(cb->pd,IB_ACCESS_REMOTE_WRITE | IB_ACCESS_LOCAL_WRITE | IB_ACCESS_REMOTE_READ);
    if (IS_ERR(cb->reg_mr))
    {
        ret = PTR_ERR(cb->reg_mr);
        pr_err( SR "reg_mr failed %d\n", ret);
        // rkit_free_buffers();
        return ret;
    }
    rkey = cb->reg_mr->rkey;
    return rkey;
}

/**
 * @brief Configures work requests for RDMA data transfer operations.
 *
 * This function is responsible for configuring work requests (WRs) that are used in
 * Remote Direct Memory Access (RDMA) data transfer operations between a server and client.
 * It performs the following tasks:
 *
 * 1. Sets up receive work requests to receive data messages from the client.
 * 2. Configures send work requests to transfer rkit_data between the server and client.
 *
 * The function initializes the structure and parameters required for WRs, such as addresses,
 * lengths, local keys, and other WR-specific settings.
 */

static void rkit_setup_wr(void)
{
    int i;

    // setup send work request
    // this work request is used to transfer rkit_data between server and client
	cb->send_sge.addr = cb->send_dma_addr;
	cb->send_sge.length = sizeof (struct rkit_data);
	cb->send_sge.lkey = cb->pd->local_dma_lkey;
	cb->send_wr.sg_list = &cb->send_sge;
	cb->send_wr.num_sge = 1;
	cb->send_wr.opcode = IB_WR_SEND;
	cb->send_wr.send_flags = IB_SEND_SIGNALED;
}

/**
 * @brief Sets up memory buffers for RDMA data transfer.
 *
 * This function is responsible for setting up memory buffers to facilitate data transfer in
 * Remote Direct Memory Access (RDMA) communication. It performs the following tasks:
 *
 * 1. Maps receive buffers to their corresponding physical addresses.
 * 2. Maps the send buffer to its physical address.
 * 3. Initializes work requests for data transfer.
 *
 * @return 0 on success, or an error code on failure.
 */
static int rkit_setup_buffers(void)
{
    int ret = 0;
    int i;
    cb->send_dma_addr = ib_dma_map_single(cb->pd->device, &cb->send_buf, sizeof(struct rkit_data), DMA_BIDIRECTIONAL);
    rkit_setup_wr();
    DEBUG_LOG(SR "allocated & registered buffers...\n");
    return ret;
}

/**
 * @brief Accept an incoming connection request from a client.
 * This function accepts an incoming connection request from a client and waits for
 * the connection to be established. It initializes the connection parameters and
 * uses the rdma_accept function to accept the connection.
 */
static int rkit_accept(void){
    int ret = 0;
    struct rdma_conn_param conn_param;

    DEBUG_LOG(SR "Accepting client connection request\n");

    memset(&conn_param, 0, sizeof conn_param);
    conn_param.responder_resources = 1;
    conn_param.initiator_depth = 1;

    ret = rdma_accept(cb->child_cm_id[connection_number], &conn_param);
    if (ret){
        pr_err(SR "rdma_accept error: %d\n",ret);
        return ret;
    }

    wait_event_interruptible(cb->sem, cb->state[connection_number] >= CONNECTED);
    if (cb->state[connection_number] == ERROR){
        pr_err(SR "wait for CONNECTED state %d\n",
            cb->state[connection_number]);
        return -1;
    }

    return ret;
}

/**
 * @brief Connect to the server as a client.
 * This function connects to the server as a client by waiting for the CONNECT_REQUEST
 * state and checking if the requested operation is supported. It uses the control block
 * (cb) and the child connection identifier (child_cm_id) to determine the connection state.
 */
static int rkit_connect_client(void)
{
    wait_event_interruptible(cb->sem, cb->state[connection_number] >= CONNECT_REQUEST);
	if (cb->state[connection_number] != CONNECT_REQUEST) {
		pr_err(SR "wait for CONNECT_REQUEST state %d\n",
			cb->state[connection_number]);
		return -1;
	}

	if (!reg_supported(cb->child_cm_id[connection_number]->device))
		return -EINVAL;
    return 0;
}

/**
 * @brief Initializes and manages a server for Remote Direct Memory Access (RDMA) communications.
 *
 * This function is responsible for setting up an RDMA server to handle incoming client connections.
 * It performs the following tasks:
 *
 * 1. Establishes connections with multiple clients.
 * 2. Sets up a Queue Pair (QP) for communication.
 * 3. Creates a Memory Region (MR) for data exchange.
 * 4. Posts send and receive work requests.
 * 5. Accepts incoming connections from clients.
 * 6. Sends an information message to clients containing MR details.
 * 7. Manages the completion of send work requests.
 * 8. Executes user-defined processing logic through a provided callback function.
 *
 * @param data A pointer to user-defined data, typically a callback function (rkit_processor), 
 *             to be executed for processing received data.
 *
 * @return 0 on success, or an error code on failure.
 */
int rkit_init_server(void * data)
{
    const struct ib_recv_wr *bad_wr;
    int ret = 0;
    int i;
    u32 *rkey;
    struct ib_wc wc;
    u64 buf;
    struct rkit_data *info;
    #if ENABLE_INFINTE_LOOP_PROTECTION == 1
    int max_try = 0;
    #endif
    rkit_processor pfun = (rkit_processor) data;

    rkey = kmalloc(number_of_connection_server*sizeof(u32),GFP_KERNEL);
    for(connection_number = 0; connection_number < number_of_connection_server;connection_number++)
    {
        ret = rkit_connect_client();
        if (ret)
        {
            pr_err(SR "Unable to connect to client\n");
            return ret;
        }
        DEBUG_LOG(SR "Connection request received from a client\n");

        // setup qp
        ret = rkit_setup_qp(cb->child_cm_id[connection_number]);
        if (ret)
        {
            pr_err(SR "setup_qp failed: %d\n" ,ret);
            rdma_destroy_id(cb->child_cm_id[connection_number]);
            return ret;
        }
        DEBUG_LOG(SR "Queue pair got setuped successfully\n");

        if (connection_number == 0) // we do buffer setup once before the first connection is done
        {
                // setup buffers
                ret = rkit_setup_buffers();
                if (ret)
                {
                    pr_err(SR "setup_buffers failed: %d\n", ret);
                    rkit_free_qp();
                    return ret;
                }
                DEBUG_LOG(SR "Buffers got setuped successfully\n");
        }  

        rkey[connection_number] = rkit_create_mr();
        DEBUG_LOG(SR "Memory region got setup & created\n");

        // accept
        ret = rkit_accept();
        if (ret)
        {
            pr_err(SR "connection error: %d",ret);
            rkit_free_buffers();
            rkit_free_qp();
            rdma_destroy_id(cb->child_cm_id[connection_number]);
            return ret;
        }
        DEBUG_LOG(SR "client got accepted successfully connection number=%d\n",connection_number);
    }


    for(i = 0; i < number_of_connection_server;i++)
    {
        // create info message
        DEBUG_LOG(SR "creating the info message\n");
        info = &cb->send_buf;
        info->rkey = htonl(rkey[i]);
        DEBUG_LOG(SR "rkey %x\n", rkey[i]);

        // send work request
        ret = ib_post_send(cb->qp[i], &cb->send_wr, NULL);
        if (ret)
        {
            pr_err(SR  "post send error %d\n", ret);
        }

        // wait for send work completion 
        ret = 0;
        #if ENABLE_INFINTE_LOOP_PROTECTION == 1
        max_try = 0;
        #endif
        while (!ret)
        {
            ret = ib_poll_cq(cb->cq[i], 1, &wc);
            #if ENABLE_INFINTE_LOOP_PROTECTION == 1
            max_try += 1;
            if (max_try >= RKIT_MAX_TRY_LOOP)
            {
                pr_err(SR "maximum tries reached in the loop abort!\n");
                return EINTR;
            }
            #endif
        }
    }

    // below two semaphores are used to sync client and server
    up(&server_client_sync1);
    down_interruptible(&server_client_sync2);
    
    pfun(cb);
    return 0;
}

/** 
 * @param addr The string representation of the server's IP address.
 *             Assumed to be a valid null-terminated string.
 * @param port The port number on which the server will listen for incoming connections.
 *             Assumed to be a valid port number.
 * @param pfun The processing function to be executed by the server thread.
 */
int rkit_start_server(const char *addr,const uint16_t port, rkit_processor pfun)
{
    int ret = 0;
    ret = rkit_pre_start(addr,port);
    if(ret)
    {
        pr_err(SR "Failed to configure server, port=%d addr=%s\n",port,addr);
        return ret;
    }

    cb->thread = kthread_create(rkit_init_server, (void*) pfun, "rkit_server");
    if (IS_ERR(cb->thread))
    {
        pr_err(SR "Failed to start server\n");
        return PTR_ERR(cb->thread);
    }
    DEBUG_LOG(SR "Starting server..."); 

    wake_up_process(cb->thread);
    return ret;
}

/**
 * @brief Stop the RDMA server and release associated resources.
 * This function stops the RDMA server by terminating its thread, disconnecting from connected
 * clients, destroying connection management identifiers (cm_id), freeing RDMA Queue Pairs (QPs),
 * and releasing the control block (cb) object along with associated buffers.
 */
void rkit_stop_server(void)
{
    int i;
    printk(SR "Server stoped...\n");
    if (cb->thread) {
        // send_sig(SIGKILL, cb->thread, 1);
        kthread_stop(cb->thread);
        DEBUG_LOG(SR "Server thread got killed\n");
    }

    rkit_free_buffers();

    DEBUG_LOG(SR "disconnect from client");
    for(i = 0; i < number_of_connection_server;i++){
        if(cb->cm_id[i]){
            rdma_disconnect(cb->cm_id[i]);
            DEBUG_LOG(SR "disconnect cm_id[%d] %p\n",i ,cb->cm_id[i]);
        }
    }
    for(i = 0; i < number_of_connection_server;i++){
        if(cb->cm_id[i]){
            DEBUG_LOG(SR "destroy cm_id[%d] %p\n",i ,cb->cm_id[i]);
            rdma_destroy_id(cb->cm_id[i]);
        }
    }

    rkit_free_qp();
    DEBUG_LOG(SR "free the cb object");

    kfree(cb);
}

struct rkit_server_handler *rkit_get_server_cb(void)
{
    return cb;
}

struct rkit_server_handler* rkit_get_server_handler(void){
    return cb;
}

inline int rkit_post_recv_wr(struct ib_recv_wr * recv_wr)
{
    return ib_post_recv(cb->qp[0],recv_wr,NULL);
}

struct ib_recv_wr *rkit_create_recv_batch(struct rkit_work_request * rwrs)
{
    struct ib_recv_wr *recv_wr;
    struct ib_recv_wr *return_recv_wr;
    struct ib_recv_wr *prev_recv_wr;
    int i = 0;
    struct ib_sge *recv_sge;
    u64 recv_dma;

    while(rwrs)
    {
        if(rwrs->operation != RECV_OP)
            return NULL;

        recv_wr = kzalloc(sizeof(struct ib_recv_wr),GFP_KERNEL);
        recv_sge = kzalloc(sizeof(struct ib_sge),GFP_KERNEL);

        recv_dma = ib_dma_map_single(cb->pd->device, rwrs->virtual_address, rwrs->length, DMA_BIDIRECTIONAL);
        recv_sge->addr = recv_dma;
        recv_sge->length = rwrs->length;
        recv_sge->lkey = cb->pd->local_dma_lkey;
        recv_wr->sg_list = recv_sge;
        recv_wr->num_sge = 1;

        if(i == 0)
            return_recv_wr = recv_wr;
        else
            prev_recv_wr->next = recv_wr;
        
        prev_recv_wr = recv_wr;
        rwrs = rwrs->next;
        i = i + 1;
    }

    return return_recv_wr;
}
