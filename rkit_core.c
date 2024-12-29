#include "rkit_core.h"

char *src_address = "172.16.25.4";
char *dest_address = "172.16.25.4";
uint16_t port = 8571;

inline void process(struct rkit_server_handler *cb)
{
        DEBUG_LOG("RKIT: " "Process function started\n");
        while(!kthread_should_stop())
        {
            cond_resched();
        }
        return;
}

static int __init rkit_init(void)
{
    int ret;
    int i;

    // init parameters
    rkit_init_params(1,1,2000,1000,1000,220,1);

    // start server
    ret = rkit_start_server(src_address,port,process);
    if (ret)
    {
        pr_err(SR "Failed to start server ret=%d",ret);
        return ret;
    }

    // init client
    i = 0;
    while (i < RKIT_MAX_RETRIES)
    {
        ret = rkit_init_client(dest_address,port);
        if (ret)
        {
            i = i + 1;
            DEBUG_LOG(CL "Failed to init client ret=%d trying again...\n",ret);
            msleep(RKIT_RETRY_INTERVAL_MSEC);
            continue;
        }
        break;
    }
    if (ret)
    {
        pr_err(CL "Failed to init client maximum number of retries reached\n");
        return ret;
    }

    return 0;
}

static void __exit rkit_exit(void)
{
    rkit_stop_server();
    rkit_destroy_client();
}

module_init(rkit_init);
module_exit(rkit_exit);
MODULE_LICENSE("GPL");