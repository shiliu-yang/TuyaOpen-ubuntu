/**
 * @file at_utils.c
 * @brief at_utils module is used to provide utility functions for AT command processing
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "at_utils.h"

#include "tal_api.h"

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/

/***********************************************************
********************function declaration********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/

/***********************************************************
***********************function define**********************
***********************************************************/

OPERATE_RET at_utils_send_wait_response(TDL_TRANSPORT_HANDLE handle, const char *cmd, uint32_t cmd_len, char *response,
                                        uint32_t response_len, uint32_t timeout)
{
    OPERATE_RET rt = OPRT_OK;
    uint32_t read_len = 0, offset_len = 0;

    TUYA_CHECK_NULL_RETURN(handle, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(cmd, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(response, OPRT_INVALID_PARM);

    // Send the AT command
    rt = tdl_transport_send(handle, cmd, cmd_len);
    if (rt != OPRT_OK) {
        PR_ERR("Failed to send AT command: %s", cmd);
        return rt;
    }

    // Wait for response
    uint32_t wait_cnt = 0;
    while (1) {
        if (wait_cnt * 10 >= timeout) {
            PR_ERR("AT command response timeout");
            return OPRT_TIMEOUT;
        }

        read_len = tdl_transport_available(handle);
        if (read_len > 0) {
            // Read response
            wait_cnt = 0;
            break;
        }

        wait_cnt++;
        tal_system_sleep(10); // Sleep for 10 ms
    }

    memset(response, 0, response_len); // Clear the response buffer

    offset_len = 0;
    int no_data_count = 0;
    do {
        read_len = tdl_transport_available(handle);
        if (read_len == 0) {
            no_data_count++;
            if (no_data_count > 10) { // 如果连续10次没有数据，退出
                break;
            }
            tal_system_sleep(10);
            continue;
        }

        no_data_count = 0; // 重置无数据计数

        // 限制每次读取的数据量，避免读取过多
        uint32_t max_read = response_len - offset_len - 1;
        if (max_read <= 0) {
            PR_ERR("Response buffer full, current: %d, max: %d", offset_len, response_len);
            break;
        }

        uint32_t actually_read = tdl_transport_read(handle, response + offset_len, max_read);
        if (actually_read == 0) {
            no_data_count++;
            if (no_data_count > 5) {
                break;
            }
        } else {
            offset_len += actually_read;
        }

        if (offset_len >= response_len - 1) {
            PR_ERR("Response buffer overflow, increase response_len, current: %d, required: %d", response_len,
                   offset_len + 1);
            return OPRT_COM_ERROR;
        }

        tal_system_sleep(10); // Sleep for 10 ms
    } while (1);

    if (offset_len == 0) {
        PR_ERR("Failed to read AT response - no data received");
        return OPRT_COM_ERROR;
    }

    // 确保字符串以null结尾
    response[offset_len] = '\0';

    PR_DEBUG("AT response (%d bytes): %s", offset_len, response);

    return rt;
}
