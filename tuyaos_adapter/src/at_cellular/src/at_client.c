/**
 * @file at_client.c
 * @brief at_client module is used to manage AT commands
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "at_client.h"
#include "at_modem.h"

#include "tdl_transport_manage.h"

#include "tal_thread.h"
#include "tal_system.h"
#include "tal_log.h"
#include "tal_queue.h"
#include "tal_mutex.h"

/***********************************************************
************************macro define************************
***********************************************************/
#define AT_CLIENT_MAGIC 0x12345678 // Magic number for validation

#define AT_CLIENT_STATUS_CHANGE(new_status)                                                                            \
    do {                                                                                                               \
        PR_DEBUG("AT client status changed: [%s] --> [%s]", AT_CLIENT_STATUS_STR[sg_at_client.status],                 \
                 AT_CLIENT_STATUS_STR[new_status]);                                                                    \
        sg_at_client.status = new_status;                                                                              \
    } while (0)

#define DEFAULT_SLEEP_TIME_MS 50 // Default sleep time in milliseconds

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef uint8_t AT_CLIENT_STATUS_T;
#define AT_CLIENT_STATUS_UNINITIALIZED 0x00 // Client uninitialized
#define AT_CLIENT_STATUS_INITIALIZED   0x01 // Client initialized
#define AT_CLIENT_STATUS_READY         0x02 // Client ready for commands
#define AT_CLIENT_STATUS_SENDING       0x03 // Client sending commands
#define AT_CLIENT_STATUS_RECEIVING     0x04 // Client receiving responses
#define AT_CLIENT_STATUS_PARSED        0x05 // Client parsed response
#define AT_CLIENT_STATUS_ERROR         0x06 // Client encountered an error

typedef struct {
    uint32_t magic; // Magic number for validation

    THREAD_HANDLE thread_hdl;
    MUTEX_HANDLE mutex;
    QUEUE_HANDLE queue;
    AT_CLIENT_STATUS_T status;

    char transport_name[TDL_TRANSPORT_NAME_MAX_LEN]; // Name of the transport layer
    TDL_TRANSPORT_HANDLE transport_hdl;              // Handle for transport layer
} AT_CLIENT_T;

/***********************************************************
********************function declaration********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/
static char *AT_CLIENT_STATUS_STR[] = {"UNINITIALIZED", "INITIALIZED", "READY", "SENDING",
                                       "RECEIVING",     "PARSED",      "ERROR"};

static AT_CLIENT_T sg_at_client = {
    .magic = AT_CLIENT_MAGIC,
    .thread_hdl = NULL,
    .mutex = NULL,
    .queue = NULL,
    .status = AT_CLIENT_STATUS_UNINITIALIZED,
};
/***********************************************************
***********************function define**********************
***********************************************************/

static void __at_client_thread(void *arg)
{
    OPERATE_RET rt = OPRT_OK;
    uint32_t delay_ms = 100; // Delay in milliseconds

    for (;;) {
        switch (sg_at_client.status) {
        case AT_CLIENT_STATUS_UNINITIALIZED: { // initialization transport
            rt = tdl_transport_find(sg_at_client.transport_name, &sg_at_client.transport_hdl);
            if (rt != OPRT_OK) {
                PR_ERR("Failed to find transport: %d", rt);
                delay_ms = 5 * 1000; // Retry after 5 seconds
                AT_CLIENT_STATUS_CHANGE(AT_CLIENT_STATUS_UNINITIALIZED);
                break;
            }
            rt = tdl_transport_open(sg_at_client.transport_hdl);
            if (rt != OPRT_OK) {
                PR_ERR("Failed to open transport: %d", rt);
                delay_ms = 5 * 1000; // Retry after 5 seconds
                AT_CLIENT_STATUS_CHANGE(AT_CLIENT_STATUS_UNINITIALIZED);
                break;
            }
            AT_CLIENT_STATUS_CHANGE(AT_CLIENT_STATUS_INITIALIZED);
        } break;
        case AT_CLIENT_STATUS_INITIALIZED: { // check AT module

            // check at module initialization
            rt = at_modem_init(sg_at_client.transport_hdl);
            if (rt != OPRT_OK) {
                PR_ERR("Failed to initialize AT modem: %d", rt);
                delay_ms = 5 * 1000; // Retry after 5 seconds
                break;
            }

            AT_CLIENT_STATUS_CHANGE(AT_CLIENT_STATUS_READY);
        } break;
        case AT_CLIENT_STATUS_READY: {
            if (DEFAULT_SLEEP_TIME_MS != delay_ms) {
                delay_ms = DEFAULT_SLEEP_TIME_MS; // Reset delay to default
            }
        } break;
        case AT_CLIENT_STATUS_SENDING: {
            PR_DEBUG("AT client is sending commands");
        } break;
        case AT_CLIENT_STATUS_RECEIVING: {
            PR_DEBUG("AT client is receiving responses");
        } break;
        case AT_CLIENT_STATUS_PARSED: {
            PR_DEBUG("AT client has parsed response");
        } break;
        case AT_CLIENT_STATUS_ERROR: {
            PR_ERR("AT client encountered an error");
        } break;
        default: {
            PR_ERR("Unknown AT client status: %d", sg_at_client.status);
        } break;
        }

        tal_system_sleep(delay_ms);
    }
}

OPERATE_RET at_client_init(char *transport_name)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(transport_name, OPRT_INVALID_PARM);
    memcpy(sg_at_client.transport_name, transport_name, TDL_TRANSPORT_NAME_MAX_LEN);

    // Create and start the AT client thread
    THREAD_CFG_T thrd_param = {4096, 4, "at_client"};
    TUYA_CALL_ERR_RETURN(
        tal_thread_create_and_start(&sg_at_client.thread_hdl, NULL, NULL, __at_client_thread, NULL, &thrd_param));

    PR_DEBUG("AT client initialized successfully");

    return rt;
}
