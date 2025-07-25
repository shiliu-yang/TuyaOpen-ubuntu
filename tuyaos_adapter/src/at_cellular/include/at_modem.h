/**
 * @file at_modem.h
 * @brief at_modem module is used to
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __AT_MODEM_H__
#define __AT_MODEM_H__

#include "tuya_cloud_types.h"

#include "tdl_transport_manage.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/
#define AT_MODEM_NAME_MAX_LEN 32 // Maximum length for AT modem name

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct at_vendor_ops {
    char name[AT_MODEM_NAME_MAX_LEN]; // Vendor name

    OPERATE_RET (*init)(TDL_TRANSPORT_HANDLE handle); // Initialize vendor-specific AT commands
} AT_VENDOR_OPS_T;
/***********************************************************
********************function declaration********************
***********************************************************/

OPERATE_RET at_modem_init(TDL_TRANSPORT_HANDLE handle);

OPERATE_RET at_modem_register_vendor(AT_VENDOR_OPS_T *vendor_ops);

#ifdef __cplusplus
}
#endif

#endif /* __AT_MODEM_H__ */
