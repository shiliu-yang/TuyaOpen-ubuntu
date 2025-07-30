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

#include <arpa/inet.h>
#include <string.h>

/***********************************************************
************************macro define************************
***********************************************************/
#define AT_SOCKET_NUM_MAX (6)

#define AT_MODEM_TIMEOUT_MS (3 * 1000)

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

static uint8_t *g_at_recv_buffer = NULL;
static uint32_t g_at_recv_buffer_size = 0;

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
        tal_system_sleep(1000); // Wait before retrying
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

    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&sg_at_vendor.mutex));

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
    // TUYA_CALL_ERR_RETURN(at_vendor_ml307_check_mt_mode());
    // TUYA_CALL_ERR_RETURN(at_vendor_ml307_check_network_registration());

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

    char send_cmd[128] = {0};
    snprintf(send_cmd, 128, "%s=%d,\"%s\",\"%s\",%d\r", AT_MIPOPEN, fd, GET_PROTOCOL_TYPE(sg_socket[fd].type),
             sg_socket[fd].ip_addr, sg_socket[fd].port);

    // PR_DEBUG("-->send_cmd: %s", send_cmd);

    at_utils_send_wait_response(sg_at_vendor.transport_hdl, send_cmd, strlen(send_cmd), g_at_response_buffer,
                                response_buffer_size, 5 * 1000);

    // PR_DEBUG("-->response: %s", g_at_response_buffer);
    if (strstr(g_at_response_buffer, OK) == NULL) {
        PR_ERR("Failed to open socket connection: %s", g_at_response_buffer);
        return OPRT_COM_ERROR;
    }

    return rt;
}

// AT+MIPCLOSE=1
OPERATE_RET at_vendor_ml307_socket_close(int fd)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(sg_at_vendor.transport_hdl, OPRT_INVALID_PARM);

    if (sg_socket[fd].is_used == 0) {
        PR_ERR("Socket fd %d is not in use", fd);
        return OPRT_INVALID_PARM;
    }

    char send_cmd[32] = {0};
    snprintf(send_cmd, sizeof(send_cmd), "AT+MIPCLOSE=%d\r", fd);
    at_utils_send_wait_response(sg_at_vendor.transport_hdl, send_cmd, strlen(send_cmd), g_at_response_buffer,
                                response_buffer_size, AT_MODEM_TIMEOUT_MS);

    if (strstr(g_at_response_buffer, OK) == NULL) {
        PR_ERR("Failed to close socket %d: %s", fd, g_at_response_buffer);
        return OPRT_COM_ERROR;
    }

    // Reset the socket state
    sg_socket[fd].is_used = 0;
    memset(sg_socket[fd].ip_addr, 0, sizeof(sg_socket[fd].ip_addr));
    sg_socket[fd].port = 0;

    return rt;
}

// AT+MDNSCFG="ip"
// +MIPCFG: "ip","114.114.114.114","8.8.8.8"
// OK
OPERATE_RET __at_vendor_ml307_check_dns_cfg(void)
{
    OPERATE_RET rt = OPRT_OK;
    char *send_cmd = "AT+MDNSCFG=\"ip\"\r";

    // Check if the DNS configuration is set
    TUYA_CHECK_NULL_RETURN(sg_at_vendor.transport_hdl, OPRT_COM_ERROR);

    at_utils_send_wait_response(sg_at_vendor.transport_hdl, send_cmd, strlen(send_cmd), g_at_response_buffer,
                                response_buffer_size, AT_MODEM_TIMEOUT_MS);

    if (strstr(g_at_response_buffer, OK) == NULL || rt != OPRT_OK) {
        PR_ERR("DNS configuration check failed: %s", g_at_response_buffer);
        return OPRT_COM_ERROR;
    }

    return rt;
}

// AT+MDNSGIP="h6.iot-dns.com"
// +MDNSGIP: "h6.iot-dns.com","2402:4E00:31:801::502","47.103.71.77","101.132.61.178","47.116.185.69"
OPERATE_RET at_vendor_ml307_gethostbyname(const char *domain, TUYA_IP_ADDR_T *addr)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(sg_at_vendor.transport_hdl, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(addr, OPRT_INVALID_PARM);

    TUYA_CALL_ERR_RETURN(__at_vendor_ml307_check_dns_cfg());

    char send_cmd[128] = {0};
    snprintf(send_cmd, sizeof(send_cmd), "AT+MDNSGIP=\"%s\"\r", domain);

    at_utils_send_wait_response(sg_at_vendor.transport_hdl, send_cmd, strlen(send_cmd), g_at_response_buffer,
                                response_buffer_size, AT_MODEM_TIMEOUT_MS);

    if (strstr(g_at_response_buffer, OK) == NULL) {
        PR_ERR("DNS query failed: %s", g_at_response_buffer);
        return OPRT_COM_ERROR;
    }

    // Parse the response to extract the IP address
    // Response format: +MDNSGIP: "domain","ipv6","ipv4","ipv4",...
    char *ip_start = strstr(g_at_response_buffer, domain);
    if (ip_start == NULL) {
        PR_ERR("Failed to find domain in response: %s", g_at_response_buffer);
        return OPRT_COM_ERROR;
    }

    // Skip domain part and find first IPv4 address (skip IPv6 if present)
    ip_start = strchr(ip_start, ','); // Find first comma after domain
    if (ip_start == NULL) {
        PR_ERR("Invalid response format: %s", g_at_response_buffer);
        return OPRT_COM_ERROR;
    }

    // Skip comma and quote
    ip_start++;
    while (*ip_start == ' ' || *ip_start == '"') {
        ip_start++;
    }

    // Check if this is IPv6 (contains ':'), if so skip to next IP
    if (strchr(ip_start, ':') != NULL && strchr(ip_start, ':') < strchr(ip_start, '"')) {
        // This is IPv6, skip to next IP address
        char *next_comma = strchr(ip_start, ',');
        if (next_comma != NULL) {
            ip_start = next_comma + 1;
            while (*ip_start == ' ' || *ip_start == '"') {
                ip_start++;
            }
        }
    }

    // Extract IPv4 address
    char ip_str[16] = {0};
    char *ip_end = strchr(ip_start, '"');
    if (ip_end == NULL || (ip_end - ip_start) >= sizeof(ip_str)) {
        PR_ERR("Failed to extract IP address from response: %s", g_at_response_buffer);
        return OPRT_COM_ERROR;
    }

    strncpy(ip_str, ip_start, ip_end - ip_start);
    ip_str[ip_end - ip_start] = '\0';

    PR_DEBUG("Extracted IP address: %s", ip_str);

    // Convert string IP to TUYA_IP_ADDR_T
    struct in_addr inaddr;
    if (inet_aton(ip_str, &inaddr) == 0) {
        PR_ERR("Invalid IP address format: %s", ip_str);
        return OPRT_COM_ERROR;
    }

    *addr = ntohl(inaddr.s_addr);

    return rt;
}

// AT+MIPSEND=1,11,"12345678900"
OPERATE_RET at_vendor_ml307_send(const int fd, const void *buf, const uint32_t nbytes)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(sg_at_vendor.transport_hdl, OPRT_INVALID_PARM);
#if 0
    char send_cmd[128] = {0};
    snprintf(send_cmd, sizeof(send_cmd), "AT+MIPSEND=%d,%d,\"", fd, nbytes);
    tdl_transport_send(sg_at_vendor.transport_hdl, send_cmd, strlen(send_cmd));
    tdl_transport_send(sg_at_vendor.transport_hdl, buf, nbytes);
    tdl_transport_send(sg_at_vendor.transport_hdl, "\"\r\n", 2);

    PR_HEXDUMP_DEBUG("Sending data to socket", buf, nbytes);

    at_utils_read_with_timeout(sg_at_vendor.transport_hdl, g_at_response_buffer, response_buffer_size, 10 * 1000);
#elif 1
    // 透传模式
    // AT + MIPMODE = 0, 1
    char send_cmd[64] = {0};
    snprintf(send_cmd, sizeof(send_cmd), "AT+MIPMODE=%d,1\r", fd);
    at_utils_send_wait_response(sg_at_vendor.transport_hdl, send_cmd, strlen(send_cmd), g_at_response_buffer,
                                response_buffer_size, AT_MODEM_TIMEOUT_MS);
    if (strstr(g_at_response_buffer, "CONNECT") == NULL) {
        PR_ERR("Failed to set MIP mode: %s", g_at_response_buffer);
        return OPRT_COM_ERROR;
    }

    // PR_HEXDUMP_DEBUG("Sending data to socket", (uint8_t *)buf, nbytes);
    tdl_transport_send(sg_at_vendor.transport_hdl, buf, nbytes);

    uint32_t cnt = 0;
    uint32_t read_len = 0;
    uint32_t read_offset = 0;
    do {
        read_len = tdl_transport_available(sg_at_vendor.transport_hdl);
        if (read_len == 0) {
            cnt++;
            if (cnt * 50 > 2 * 1000) {
                PR_DEBUG("No data received after 2 seconds, exiting send loop");
                break;
            } else {
                tal_system_sleep(50);
                continue;
            }
        }
        cnt = 0;

        read_len =
            tdl_transport_read(sg_at_vendor.transport_hdl, (uint8_t *)g_at_response_buffer, response_buffer_size);

        if (g_at_recv_buffer == NULL) {
            g_at_recv_buffer = (uint8_t *)tal_malloc(response_buffer_size);
            if (g_at_recv_buffer == NULL) {
                PR_ERR("Failed to allocate memory for receive buffer");
                rt = OPRT_MALLOC_FAILED;
                break;
            }
            memset(g_at_recv_buffer, 0, response_buffer_size);
        } else {
            g_at_recv_buffer = (uint8_t *)tal_realloc(g_at_recv_buffer, read_len + read_offset);
            if (g_at_recv_buffer == NULL) {
                PR_ERR("Failed to reallocate memory for receive buffer");
                rt = OPRT_MALLOC_FAILED;
                break;
            }
        }
        PR_DEBUG("Received %u bytes from transport, total: %u", read_len, read_offset);

        memcpy(g_at_recv_buffer + read_offset, g_at_response_buffer, read_len);

        read_offset += read_len;
        g_at_recv_buffer_size = read_offset;

        // PR_HEXDUMP_DEBUG("Received data from socket", (uint8_t *)g_at_response_buffer, read_len);
        tal_system_sleep(50);
    } while (1);

    at_utils_send_wait_response(sg_at_vendor.transport_hdl, "+++", strlen("+++"), g_at_response_buffer,
                                response_buffer_size, AT_MODEM_TIMEOUT_MS);
    // response OK

    PR_DEBUG("send finished, response: %s", g_at_response_buffer);
#else
    // TCP 模式配置为 HEX 模式
    // AT+MIPCFG="encoding",0,1,0 //输入配置为HEX模式
    // AT +MIPCFG="encoding"[,<connect_id>[,<send_format>,<recv_format>]]
    // send_format: 0 - ASCII, 1 - HEX, 2 - 带转义的字符串
    // recv_format: 0 - ASCII, 1 - HEX
    char send_cmd[128] = {0};
    snprintf(send_cmd, sizeof(send_cmd), "AT+MIPCFG=\"encoding\",%d,%d,%d", fd, 1, 1);
    at_utils_send_wait_response(sg_at_vendor.transport_hdl, send_cmd, strlen(send_cmd), g_at_response_buffer,
                                response_buffer_size, AT_MODEM_TIMEOUT_MS);
    if (strstr(g_at_response_buffer, OK) == NULL) {
        PR_ERR("Failed to set encoding mode: %s", g_at_response_buffer);
        return OPRT_COM_ERROR;
    }

    PR_HEXDUMP_DEBUG("Sending data to socket", (uint8_t *)buf, nbytes);

    //
    uint32_t _send_len = 2 * nbytes + 32;
    uint32_t _send_offset = 0;
    char *_send_data = tal_malloc(_send_len);
    if (_send_data == NULL) {
        PR_ERR("Failed to allocate memory for send data");
        return OPRT_MALLOC_FAILED;
    }
    memset(_send_data, 0, _send_len);
    _send_offset += snprintf(_send_data, _send_len, "AT+MIPSEND=%d,%d,\"", fd, nbytes);

    for (uint32_t i = 0; i < nbytes; i++) {
        _send_offset += snprintf(_send_data + _send_offset, _send_len - _send_offset, "%02X", ((uint8_t *)buf)[i]);
    }
    _send_offset += snprintf(_send_data + _send_offset, _send_len - _send_offset, "\"\r");

    PR_DEBUG("Sending command: %s", _send_data);

    tdl_transport_send(sg_at_vendor.transport_hdl, _send_data, _send_offset);
    tal_free(_send_data);

    at_utils_read_with_timeout(sg_at_vendor.transport_hdl, g_at_response_buffer, response_buffer_size, 10 * 1000);
#endif
    return rt;
}

OPERATE_RET at_vendor_ml307_read(const int fd, void *buf, const uint32_t nbytes)
{
    TUYA_CHECK_NULL_RETURN(buf, OPRT_INVALID_PARM);

    if (g_at_recv_buffer == NULL) {
        return OPRT_COM_ERROR;
    }

    uint32_t read_len = (g_at_recv_buffer_size < nbytes) ? g_at_recv_buffer_size : nbytes;
    if (read_len > 0) {
        memcpy(buf, g_at_recv_buffer, read_len);
        // Shift remaining data to the front of the buffer
        memmove(g_at_recv_buffer, g_at_recv_buffer + read_len, g_at_recv_buffer_size - read_len);
        g_at_recv_buffer_size -= read_len;

        // Clean up buffer if empty
        if (g_at_recv_buffer_size == 0) {
            tal_free(g_at_recv_buffer);
            g_at_recv_buffer = NULL;
            g_at_recv_buffer_size = 0;
        }

        return read_len;
    } else {
        PR_ERR("No data available to read");
        return 0;
    }
}

OPERATE_RET at_vendor_ml307_register(void)
{
    AT_VENDOR_OPS_T vendor_ops = {
        .name = "ML307",
        .init = at_vendor_ml307_init,
    };

    return at_modem_register_vendor(&vendor_ops);
}
