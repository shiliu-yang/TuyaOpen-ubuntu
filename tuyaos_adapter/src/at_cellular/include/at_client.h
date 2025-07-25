/**
 * @file at_client.h
 * @brief at_client module is used to
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __AT_CLIENT_H__
#define __AT_CLIENT_H__

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

OPERATE_RET at_client_init(char *transport_name);

#ifdef __cplusplus
}
#endif

#endif /* __AT_CLIENT_H__ */
