/**
 * @file at_socket.c
 * @brief at_socket module is used to manage socket connections in cellular networks.
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "at_socket.h"

#include "tal_log.h"
#include "tal_mutex.h"

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct {
    uint8_t is_init;

    MUTEX_HANDLE mutex; // Mutex for thread safety
} AT_SOCKET_T;

/***********************************************************
********************function declaration********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/
static int g_errno = 0; // Global variable to store error number

static AT_SOCKET_T sg_at_socket = {
    .is_init = 0,
    .mutex = NULL, // Initialize mutex to NULL
};

/***********************************************************
***********************function define**********************
***********************************************************/

static OPERATE_RET __at_socket_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    if (sg_at_socket.is_init) {
        return OPRT_OK; // Already initialized
    }

    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&sg_at_socket.mutex));

    sg_at_socket.is_init = 1;
    return rt; // Return success
}

int at_get_errno(void)
{
    return g_errno; // Return the current error number
}

void at_set_errno(int errno)
{
    if (!sg_at_socket.is_init) {
        __at_socket_init();
    }

    tal_mutex_lock(sg_at_socket.mutex);
    g_errno = errno; // Set the global error number
    tal_mutex_unlock(sg_at_socket.mutex);
}

int at_socket(int domain, int type, int protocol) {}
