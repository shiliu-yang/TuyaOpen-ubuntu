/**
 * @file at_vendor_ml307.c
 * @brief at_vendor_ml307 module is used to
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "at_vendor_ml307.h"

#include "at_modem.h"
#include "at_utils.h"

#include "tdl_transport_manage.h"

#include "tal_api.h"

/***********************************************************
************************macro define************************
***********************************************************/
#define AT_SOCKET_NUM_MAX (6)

#define AT_MODEM_TIMEOUT_MS (1000)

#define AT         "AT\r"        // AT command prefix
#define AT_CPIN    "AT+CPIN?\r"  // Command to check SIM card status
#define AT_CFUN    "AT+CFUN?\r"  // Command to check MT functionality mode
#define AT_CEREG   "AT+CEREG?\r" // Command to check network registration status
#define AT_MIPOPEN "AT+MIPOPEN"

// Response strings
#define OK "OK"

//     PROTOCOL_TCP = 0,
// PROTOCOL_UDP = 1,
#define GET_PROTOCOL_TYPE(type) ((type) == PROTOCOL_TCP ? "TCP" : "UDP")

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct {
    TDL_TRANSPORT_HANDLE transport_hdl;
    MUTEX_HANDLE mutex;
} AT_VENDOR_T;

typedef struct {
    uint8_t is_used;

    TUYA_PROTOCOL_TYPE_E type;
    char ip_addr[16];
    uint16_t port;
} AT_SOCKET_T;

/***********************************************************
********************function declaration********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/
#define response_buffer_size 4096         // Size of the response buffer (increased from 1024)
static char *g_at_response_buffer = NULL; // Buffer for AT command responses

static AT_VENDOR_T sg_at_vendor = {
    .transport_hdl = NULL,
};

static AT_SOCKET_T sg_socket[AT_SOCKET_NUM_MAX] = {0};

/***********************************************************
***********************function define**********************
***********************************************************/

static int __at_vendor_ml307_get_unused_socket()
{
    for (int i = 0; i < AT_SOCKET_NUM_MAX; i++) {
        if (sg_socket[i].is_used == 0) {
            return i;
        }
    }
    return -1;
}

OPERATE_RET at_vendor_ml307_is_connected(void)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(sg_at_vendor.transport_hdl, OPRT_INVALID_PARM);

#if 1
    for (int i = 0; i < 5; i++) {
        tdl_transport_config(sg_at_vendor.transport_hdl, TDL_TRANSPORT_CMD_RX_BUFFER_RESET, NULL);
        rt = at_utils_send_wait_response(sg_at_vendor.transport_hdl, AT, strlen(AT), g_at_response_buffer,
                                         response_buffer_size, AT_MODEM_TIMEOUT_MS);
        if (rt == OPRT_OK && strstr(g_at_response_buffer, OK) != NULL) {
            break; // Exit loop if command was sent successfully
        }
        tal_system_sleep(200); // Wait before retrying
    }
#else
    TUYA_CALL_ERR_RETURN(at_utils_send_wait_response(sg_at_vendor.transport_hdl, AT, strlen(AT), g_at_response_buffer,
                                                     response_buffer_size, AT_MODEM_TIMEOUT_MS));

    if (strstr(g_at_response_buffer, OK) == NULL) {
        PR_ERR("Module is not connected or AT command failed");
        return OPRT_COM_ERROR;
    }
#endif
    return OPRT_OK;
}

// 查询SIM卡是否初始化成功
OPERATE_RET at_vendor_ml307_check_sim_status(void)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(sg_at_vendor.transport_hdl, OPRT_INVALID_PARM);

    TUYA_CALL_ERR_RETURN(at_utils_send_wait_response(sg_at_vendor.transport_hdl, AT_CPIN, strlen(AT_CPIN),
                                                     g_at_response_buffer, response_buffer_size, AT_MODEM_TIMEOUT_MS));

    if (strstr(g_at_response_buffer, OK) == NULL) {
        PR_ERR("SIM card is not initialized or AT command failed");
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

// 查询MT功能模式
OPERATE_RET at_vendor_ml307_check_mt_mode(void)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(sg_at_vendor.transport_hdl, OPRT_INVALID_PARM);

    TUYA_CALL_ERR_RETURN(at_utils_send_wait_response(sg_at_vendor.transport_hdl, AT_CFUN, strlen(AT_CFUN),
                                                     g_at_response_buffer, response_buffer_size, AT_MODEM_TIMEOUT_MS));

    if (strstr(g_at_response_buffer, "1") == NULL) {
        PR_ERR("MT functionality mode check failed");
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

// 查询是否驻网成功
OPERATE_RET at_vendor_ml307_check_network_registration(void)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(sg_at_vendor.transport_hdl, OPRT_INVALID_PARM);

    TUYA_CALL_ERR_RETURN(at_utils_send_wait_response(sg_at_vendor.transport_hdl, AT_CEREG, strlen(AT_CEREG),
                                                     g_at_response_buffer, response_buffer_size, AT_MODEM_TIMEOUT_MS));

    if (strstr(g_at_response_buffer, OK) == NULL) {
        PR_ERR("Network registration check failed");
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

OPERATE_RET at_vendor_ml307_init(TDL_TRANSPORT_HANDLE handle)
{
    OPERATE_RET rt = OPRT_OK;
    uint32_t read_len = 0;

    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(sg_at_vendor.mutex));

    // Check if the module is connected
    if (g_at_response_buffer == NULL) {
        g_at_response_buffer = (char *)tal_malloc(response_buffer_size); // Allocate buffer for AT response
        if (g_at_response_buffer == NULL) {
            PR_ERR("Failed to allocate memory for AT response buffer");
            return OPRT_MALLOC_FAILED;
        }
    }

    sg_at_vendor.transport_hdl = handle;

    TUYA_CALL_ERR_RETURN(at_vendor_ml307_is_connected());
    TUYA_CALL_ERR_RETURN(at_vendor_ml307_check_sim_status());
    TUYA_CALL_ERR_RETURN(at_vendor_ml307_check_mt_mode());
    TUYA_CALL_ERR_RETURN(at_vendor_ml307_check_network_registration());

    return rt;
}

OPERATE_RET at_vendor_ml307_socket_create(int *sock_fd, const TUYA_PROTOCOL_TYPE_E type)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(sock_fd, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(sg_at_vendor.mutex, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(sg_at_vendor.transport_hdl, OPRT_INVALID_PARM);

    tal_mutex_lock(sg_at_vendor.mutex);

    int fd = __at_vendor_ml307_get_unused_socket();
    if (fd < 0) {
        rt = OPRT_COM_ERROR;
        goto __EXIT;
    }

    sg_socket[fd].is_used = 1;
    sg_socket[fd].type = type;

__EXIT:
    *sock_fd = fd;

    tal_mutex_unlock(sg_at_vendor.mutex);

    return rt;
}

// AT+MIPOPEN=0,"UDP","120.27.12.119",2016,60,0 //0#建立UDP连接。
// OK
// +MIPOPEN: 0,0 //0#建立成功。
OPERATE_RET at_vendor_ml307_socket_connect(int fd, const char *addr, const uint16_t port)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(sg_at_vendor.transport_hdl, OPRT_INVALID_PARM);

    if (fd >= AT_SOCKET_NUM_MAX) {
        PR_ERR("Invalid socket fd");
        return OPRT_INVALID_PARM;
    }

    if (sg_socket[fd].is_used == 0) {
        return OPRT_INVALID_PARM;
    }

    strncpy(sg_socket[fd].ip_addr, addr, 16);
    sg_socket[fd].port = port;

    char send_cmd[64] = {0};
    sprintf(send_cmd, 64, "%s=%d,\"%s\",\"%s\",%d\r", AT_MIPOPEN, fd, GET_PROTOCOL_TYPE(sg_socket[fd].type),
            sg_socket[fd].ip_addr, sg_socket[fd].port);

    PR_DEBUG("-->send_cmd: %s", send_cmd);

    at_utils_send_wait_response(sg_at_vendor.transport_hdl, send_cmd, strlen(send_cmd), g_at_response_buffer,
                                response_buffer_size, 5 * 1000);

    PR_DEBUG("-->response: %s", g_at_response_buffer);

    return rt;
}

OPERATE_RET at_vendor_ml307_register(void)
{
    AT_VENDOR_OPS_T vendor_ops = {
        .name = "ML307",
        .init = at_vendor_ml307_init,
    };

    return at_modem_register_vendor(&vendor_ops);
}
