/**
 * @file at_modem.c
 * @brief at_modem module is used to
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "at_modem.h"

#include "at_vendor_ml307.h"

#include "tal_log.h"

/***********************************************************
************************macro define************************
***********************************************************/
#define AT_MODEM_MAGIC 0x12345678 // Magic number for validation

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct {
    uint32_t magic; // Magic number for validation

    TDL_TRANSPORT_HANDLE transport_hdl; // Handle for transport layer

    AT_VENDOR_OPS_T vendor_ops; // Vendor-specific operations
} AT_MODEM_T;

/***********************************************************
********************function declaration********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/
static AT_MODEM_T sg_at_modem = {
    .magic = AT_MODEM_MAGIC,
    .transport_hdl = NULL,
};

/***********************************************************
***********************function define**********************
***********************************************************/

OPERATE_RET at_modem_init(TDL_TRANSPORT_HANDLE handle)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(handle, OPRT_INVALID_PARM);

    sg_at_modem.transport_hdl = handle;

    at_vendor_ml307_register(); // Register ML307 vendor-specific AT commands

    TUYA_CHECK_NULL_RETURN(sg_at_modem.vendor_ops.init, OPRT_INVALID_PARM);
    rt = sg_at_modem.vendor_ops.init(sg_at_modem.transport_hdl);
    if (rt != OPRT_OK) {
        PR_ERR("Failed to initialize vendor-specific AT commands: %d", rt);
        return rt;
    }

    PR_DEBUG("Modem AT module initialized successfully");

    return rt;
}

OPERATE_RET at_modem_register_vendor(AT_VENDOR_OPS_T *vendor_ops)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(vendor_ops, OPRT_INVALID_PARM);

    strncpy(sg_at_modem.vendor_ops.name, vendor_ops->name, AT_MODEM_NAME_MAX_LEN - 1);
    memcpy(&sg_at_modem.vendor_ops, vendor_ops, sizeof(AT_VENDOR_OPS_T));

    return rt;
}
