/**
 * @file at_socket.h
 * @brief at_socket module is used to manage socket connections in cellular networks.
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __AT_SOCKET_H__
#define __AT_SOCKET_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/

/***********************************************************
********************function declaration********************
***********************************************************/

int at_get_errno(void);
void at_set_errno(int errno);

int at_socket(int domain, int type, int protocol);

#ifdef __cplusplus
}
#endif

#endif /* __AT_SOCKET_H__ */
