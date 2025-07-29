/**
 * @file at_utils.h
 * @brief at_utils module is used to provide utility functions for AT command processing
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __AT_UTILS_H__
#define __AT_UTILS_H__

#include "tuya_cloud_types.h"

#include "tdl_transport_manage.h"

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

OPERATE_RET at_utils_send_wait_response(TDL_TRANSPORT_HANDLE handle, const char *cmd, uint32_t cmd_len, char *response,
                                        uint32_t response_len, uint32_t timeout);

OPERATE_RET at_utils_read_with_timeout(TDL_TRANSPORT_HANDLE handle, char *buf, uint32_t buf_size, uint32_t timeout_ms);

uint8_t at_utils_is_ipv4(const char *ip);

uint8_t at_utils_is_ipv6(const char *ip);

#ifdef __cplusplus
}
#endif

#endif /* __AT_UTILS_H__ */
