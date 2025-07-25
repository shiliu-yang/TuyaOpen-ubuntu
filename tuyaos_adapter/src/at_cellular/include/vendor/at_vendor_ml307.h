/**
 * @file at_vendor_ml307.h
 * @brief at_vendor_ml307 module is used to
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __AT_VENDOR_ML307_H__
#define __AT_VENDOR_ML307_H__

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

OPERATE_RET at_vendor_ml307_register(void);

OPERATE_RET at_vendor_ml307_socket_create(int *sock_fd, const TUYA_PROTOCOL_TYPE_E type);

OPERATE_RET at_vendor_ml307_socket_connect(int fd, const char *addr, const uint16_t port);

#ifdef __cplusplus
}
#endif

#endif /* __AT_VENDOR_ML307_H__ */
