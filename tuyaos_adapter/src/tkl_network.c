/**
 * @file tkl_network.c
 * @brief tkl_network module is used to
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "tkl_network.h"
#include "tal_api.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>

#include "at_socket.h"
#include "at_vendor_ml307.h"

/***********************************************************
************************macro define************************
***********************************************************/
#define TUYA_FD_MAX_COUNT (6) // Maximum number of file descriptors

// PROTOCOL_TCP = 0,
// PROTOCOL_UDP = 1,
#define GET_PROTOCOL_TYPE(type) ((type) == PROTOCOL_TCP ? "TCP" : "UDP")

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct {
    int is_used; // Indicates if the socket is in use

    int fd;           // File descriptor for the socket
    char ip_addr[16]; // IP address of the socket
    uint16_t port;    // Port number of the socket

    TUYA_PROTOCOL_TYPE_E type; // Protocol type (TCP/UDP)
    BOOL_T is_block;           // Indicates if the socket is blocking
    BOOL_T is_connected;       // Indicates if the socket is connected

    char *recv;
    uint32_t recv_size; // The total size of the receive buffer
    uint32_t recv_used; // Amount of data currently in the receive buffer
    MUTEX_HANDLE mutex; // Mutex for thread safety

    uint32_t recv_timeout; // Receive timeout in milliseconds
    uint32_t send_timeout; // Send timeout in milliseconds
} TUYA_SOCKET_T;

/***********************************************************
********************function declaration********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/
static TUYA_SOCKET_T sg_socket[TUYA_FD_MAX_COUNT] = {0}; // Array of sockets

/***********************************************************
***********************function define**********************
***********************************************************/

/**
 * @brief Get error code of network
 *
 * @param void
 *
 * @note This API is used for getting error code of network.
 *
 * @return 0 on success. Others on error, please refer to the error no of the target system
 */
TUYA_ERRNO tkl_net_get_errno(void)
{
    return at_get_errno(); // Placeholder for error code, replace with actual implementation
}

/**
 * @brief Add file descriptor to set
 *
 * @param[in] fd: file descriptor
 * @param[in] fds: set of file descriptor
 *
 * @note This API is used to add file descriptor to set.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_fd_set(const int fd, TUYA_FD_SET_T *fds)
{
    OPERATE_RET rt = OPRT_OK;

    // PR_DEBUG("--> [T] Adding fd: %d to set", fd);

    return rt;
}

/**
 * @brief Clear file descriptor from set
 *
 * @param[in] fd: file descriptor
 * @param[in] fds: set of file descriptor
 *
 * @note This API is used to clear file descriptor from set.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_fd_clear(const int fd, TUYA_FD_SET_T *fds)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("--> [T] Clearing fd: %d from set", fd);

    return rt;
}

/**
 * @brief Check file descriptor is in set
 *
 * @param[in] fd: file descriptor
 * @param[in] fds: set of file descriptor
 *
 * @note This API is used to check the file descriptor is in set.
 *
 * @return TRUE or FALSE
 */
OPERATE_RET tkl_net_fd_isset(const int fd, TUYA_FD_SET_T *fds)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("--> [T] Checking if fd: %d is in set", fd);

    return rt;
}

/**
 * @brief Clear all file descriptor in set
 *
 * @param[in] fds: set of file descriptor
 *
 * @note This API is used to clear all file descriptor in set.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_fd_zero(TUYA_FD_SET_T *fds)
{
    OPERATE_RET rt = OPRT_OK;

    // PR_DEBUG("--> [T] Clearing all file descriptors in set");

    return rt;
}

/**
 * @brief Get available file descriptors
 *
 * @param[in] maxfd: max count of file descriptor
 * @param[out] readfds: a set of readalbe file descriptor
 * @param[out] writefds: a set of writable file descriptor
 * @param[out] errorfds: a set of except file descriptor
 * @param[in] ms_timeout: time out
 *
 * @note This API is used to get available file descriptors.
 *
 * @return >0 the count of available file descriptors, <=0 error.
 */
int tkl_net_select(const int maxfd, TUYA_FD_SET_T *readfds, TUYA_FD_SET_T *writefds, TUYA_FD_SET_T *errorfds,
                   const uint32_t ms_timeout)
{
    // PR_DEBUG("--> [T] Selecting file descriptors with maxfd: %d, timeout: %d ms", maxfd, ms_timeout);
    // TODO: return 1
    return 1;
}

/**
 * @brief Get no block file descriptors
 *
 * @param[in] fd: file descriptor
 *
 * @note This API is used to get no block file descriptors.
 *
 * @return >0 the count of no block file descriptors, <=0 error.
 */
int tkl_net_get_nonblock(const int fd)
{
    if (fd < 0 || fd >= TUYA_FD_MAX_COUNT) {
        PR_ERR("Invalid file descriptor: %d", fd);
        return UNW_FAIL;
    }

    if (sg_socket[fd].is_used == 0) {
        PR_ERR("Socket fd: %d is not in use", fd);
        return UNW_FAIL;
    }

    return sg_socket[fd].is_block;
}

/**
 * @brief Set block flag for file descriptors
 *
 * @param[in] fd: file descriptor
 * @param[in] block: block flag
 *
 * @note This API is used to set block flag for file descriptors.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_set_block(const int fd, const BOOL_T block)
{
    OPERATE_RET rt = OPRT_OK;

    // PR_DEBUG("--> [T] Setting block for fd: %d, block: %d", fd, block);
    if (fd < 0 || fd >= TUYA_FD_MAX_COUNT) {
        PR_ERR("Invalid file descriptor: %d", fd);
        return UNW_FAIL;
    }

    if (sg_socket[fd].is_used == 0) {
        PR_ERR("Socket fd: %d is not in use", fd);
        return UNW_FAIL;
    }

    if (NULL == sg_socket[fd].mutex) {
        PR_ERR("Socket mutex is NULL for fd: %d", fd);
        return UNW_FAIL;
    }

    tal_mutex_lock(sg_socket[fd].mutex);

    sg_socket[fd].is_block = block;

    tal_mutex_unlock(sg_socket[fd].mutex);

    return rt;
}

void __tkl_socket_deinit(TUYA_SOCKET_T *socket)
{
    if (socket == NULL) {
        PR_ERR("Socket is NULL in __tkl_socket_deinit");
        return;
    }

    socket->is_used = 0;                                 // Mark the socket as unused
    socket->fd = -1;                                     // Reset file descriptor
    memset(socket->ip_addr, 0, sizeof(socket->ip_addr)); // Clear IP
    socket->port = 0;                                    // Reset port number
    socket->type = PROTOCOL_TCP;                         // Reset protocol type to default
    socket->is_block = TRUE;                             // Reset blocking mode to default
    socket->is_connected = FALSE;                        // Reset connection status
    if (socket->recv != NULL) {
        tal_free(socket->recv); // Free the receive buffer if allocated
        socket->recv = NULL;
    }
    socket->recv_size = 0; // Reset receive buffer size
    socket->recv_used = 0; // Reset used size in receive buffer

    socket->recv_timeout = 0; // Reset receive timeout
    socket->send_timeout = 0; // Reset send timeout

    return;
}

/**
 * @brief Close file descriptors
 *
 * @param[in] fd: file descriptor
 *
 * @note This API is used to close file descriptors.
 *
 * @return 0 on success. Others on error, please refer to the error no of the target system
 */
TUYA_ERRNO tkl_net_close(const int fd)
{
    PR_DEBUG("--> [T] Closing fd: %d", fd);
#if 0
    OPERATE_RET ret = at_vendor_ml307_socket_close(fd);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to close socket: %d, error: %d", fd, ret);
        return UNW_FAIL;
    }
#else
    TUYA_ERRNO func_rt = UNW_SUCCESS;
    OPERATE_RET rt = OPRT_OK;

    if (fd < 0 || fd >= TUYA_FD_MAX_COUNT) {
        PR_ERR("Invalid file descriptor: %d", fd);
        return UNW_FAIL;
    }

    if (sg_socket[fd].is_used == 0) {
        return UNW_SUCCESS;
    }

    // Close the socket
    // AT+MIPCLOSE=

    tal_mutex_lock(sg_socket[fd].mutex);

    char tmp_buf[32] = {0};
    snprintf(tmp_buf, sizeof(tmp_buf), "AT+MIPCLOSE=%d\r", fd);
    AT_LINE_T *line = NULL;
    uint32_t line_num = 0;
    rt = at_client_send(tmp_buf, strlen(tmp_buf), 1000, &line, &line_num);
    if (rt != OPRT_OK) {
        PR_ERR("Failed to send MIPCLOSE command: %d", rt);
        func_rt = UNW_FAIL;
        goto __EXIT;
    }
    PR_DEBUG("Received %d lines after sending MIPCLOSE command: %s", line_num, tmp_buf);
    if (strstr(line->data, "OK") == NULL) {
        PR_ERR("Failed to close socket %d: %s", fd, tmp_buf);
        func_rt = UNW_FAIL;
        goto __EXIT;
    }
    at_client_free_lines(line);
    at_client_get_one_line(&line);

    // +MIPCLOSE:
    if (line != NULL && strstr(line->data, "+MIPCLOSE:") != NULL) {
        PR_DEBUG("Socket %d closed successfully", fd);
        __tkl_socket_deinit(&sg_socket[fd]);
    } else {
        PR_ERR("Failed to close socket %d, no response received", fd);
        func_rt = UNW_FAIL;
        goto __EXIT;
    }

__EXIT:
    tal_mutex_unlock(sg_socket[fd].mutex);

#endif
    return func_rt;
}

/**
 * @brief Shutdown file descriptors
 *
 * @param[in] fd: file descriptor
 * @param[in] how: shutdown type
 *
 * @note This API is used to shutdown file descriptors.
 *
 * @return 0 on success. Others on error, please refer to the error no of the target system
 */
TUYA_ERRNO tkl_net_shutdown(const int fd, const int how)
{
    PR_DEBUG("--> [T] Shutting down fd: %d with how: %d", fd, how);

    return UNW_SUCCESS;
}

static int __get_unused_socket(void)
{
    for (int i = 0; i < TUYA_FD_MAX_COUNT; i++) {
        if (sg_socket[i].is_used == 0) {
            return i;
        }
    }
    return -1;
}

int hex_char_to_byte(const char *hex, size_t hex_len, uint8_t *out_buf, size_t max_len)
{
    if (hex_len % 2 != 0)
        return -1;

    size_t byte_len = hex_len / 2;
    if (byte_len > max_len)
        return -2;

    for (size_t i = 0; i < byte_len; i++) {
        char byte_str[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        out_buf[i] = (uint8_t)strtoul(byte_str, NULL, 16);
    }

    return (int)byte_len;
}

// +MIPURC: "disconn",0,1

// +MIPURC: "rtcp",0,1380,xxxxxx
void at_socket_rtcp_callback(const char *line, uint32_t length, void *user_data)
{
    if (user_data == NULL) {
        PR_ERR("User data is NULL in at_socket_rtcp_callback");
        return;
    }

    PR_DEBUG("at_socket_rtcp_callback %p: %s, length: %d", line, line, length);

    TUYA_SOCKET_T *socket = (TUYA_SOCKET_T *)user_data;

    int fd = -1;
    uint32_t data_length = 0;
    uint32_t offset = 0;

    // skip the +MIPURC: "rtcp", prefix
    offset = strlen("+MIPURC: \"rtcp\",");
    // get fd
    const char *p = line + offset;
    fd = atoi(p);
    PR_DEBUG("Socket fd: %d", fd);
    // get data length
    p = strchr(p, ',');
    if (p == NULL) {
        PR_ERR("Failed to parse data length from line: %s", line);
        return;
    }
    p++;
    data_length = atoi(p);
    PR_DEBUG("Data length: %d", data_length);

    // p points to the start of the data
    p = strchr(p, ',');
    if (p == NULL) {
        PR_ERR("Failed to parse data from line: %s", line);
        return;
    }
    p++; // Move past the comma

    //
    if (fd < 0 || fd >= TUYA_FD_MAX_COUNT) {
        PR_ERR("Invalid socket fd: %d", fd);
        return;
    }
    socket = &sg_socket[fd];
    if (socket->is_used == 0) {
        PR_ERR("Socket fd: %d is not in use", fd);
        return;
    }

    // copy data
    tal_mutex_lock(socket->mutex);
    uint32_t malloc_len = 0;
    if (socket->recv == NULL) {
        malloc_len = data_length;
        socket->recv_used = 0;
        socket->recv = tal_malloc(malloc_len);
        if (socket->recv == NULL) {
            PR_ERR("Failed to allocate memory for recv buffer");
            goto __EXIT;
        }
        socket->recv_size = malloc_len;
    } else if (socket->recv_used + data_length > socket->recv_size) {
        malloc_len = socket->recv_size + data_length;
        char *new_recv = tal_realloc(socket->recv, malloc_len);
        if (new_recv == NULL) {
            PR_ERR("Failed to reallocate memory for recv buffer");
            goto __EXIT;
        }
        socket->recv = new_recv;
        socket->recv_size = malloc_len;
    } else {
        malloc_len = socket->recv_size; // Use existing size
    }

    uint32_t available_space = malloc_len - socket->recv_used;
    if (available_space < data_length) {
        PR_ERR("Insufficient buffer space: need %d, have %d", data_length, available_space);
        goto __EXIT;
    }

    int bytes = hex_char_to_byte(p, data_length * 2, (uint8_t *)(socket->recv + socket->recv_used),
                                 malloc_len - socket->recv_used);
    if (bytes < 0) {
        PR_ERR("Failed to convert hex to bytes, error code: %d", bytes);
        goto __EXIT;
    }
    socket->recv_used += bytes;

    PR_DEBUG("Received RTCP data on fd: %d, length: %d", fd, data_length);

__EXIT:
    tal_mutex_unlock(socket->mutex);

    return;
}

AT_RESPONSE_PATTERN_T sg_socket_recv_pattern = {
    .pattern = "+MIPURC: \"rtcp\"",
    .match_type = MATCH_PREFIX,
    .response_type = AT_RESPONSE_TYPE_URC,
    .is_final = 0,
    .pattern_hash = 0,                   // Placeholder for hash, can be computed if needed
    .callback = at_socket_rtcp_callback, // Callback function to handle the response
    .user_data = &sg_socket,             // User data for the callback
    .next = NULL                         // Next pattern in the list
};

void at_socket_socket_close_callback(const char *line, uint32_t length, void *user_data)
{
    if (user_data == NULL) {
        PR_ERR("User data is NULL in at_socket_socket_close_callback");
        return;
    }

    TUYA_SOCKET_T *socket = (TUYA_SOCKET_T *)user_data;
    int fd = -1;
    // skip the +MIPURC: "disconn", prefix
    const char *p = line + strlen("+MIPURC: \"disconn\",");
    fd = atoi(p);
    PR_DEBUG("Socket fd: %d", fd);

    socket = &sg_socket[fd];
    if (socket->is_used == 0) {
        PR_ERR("Socket fd: %d is not in use", fd);
        return;
    }

    // Mark the socket as not used
    // socket->is_used = 0;
    // socket->is_connected = FALSE;
    // socket->fd = -1;                                     // Reset file descriptor
    // socket->port = 0;                                    // Reset port
    // memset(socket->ip_addr, 0, sizeof(socket->ip_addr)); // Clear IP address

    // if (socket->recv != NULL) {
    //     tal_free(socket->recv);
    //     socket->recv = NULL;
    //     socket->recv_size = 0;
    //     socket->recv_used = 0;
    // }
#if 0
    __tkl_socket_deinit(socket); // Deinitialize the socket
#else
    tkl_net_close(fd); // Close the socket using the existing close function
#endif
    return;
}

AT_RESPONSE_PATTERN_T sg_socket_close_pattern = {
    .pattern = "+MIPURC: \"disconn\"",
    .match_type = MATCH_PREFIX,
    .response_type = AT_RESPONSE_TYPE_URC,
    .is_final = 0,
    .pattern_hash = 0,                           // Placeholder for hash, can be computed if needed
    .callback = at_socket_socket_close_callback, // Callback function to handle the response
    .user_data = &sg_socket,                     // User data for the callback
    .next = NULL                                 // Next pattern in the list
};

/**
 * @brief Create a tcp/udp socket
 *
 * @param[in] type: protocol type, tcp or udp
 *
 * @note This API is used for creating a tcp/udp socket.
 *
 * @return file descriptor
 */
int tkl_net_socket_create(const TUYA_PROTOCOL_TYPE_E type)
{
    int fd = -1;
#if 0
    at_vendor_ml307_socket_create(&fd, type);
#else
    fd = __get_unused_socket();
    if (fd != -1) {
        memset(&sg_socket[fd], 0, sizeof(TUYA_SOCKET_T));
        sg_socket[fd].is_used = 1;
        sg_socket[fd].type = type;
        sg_socket[fd].is_connected = FALSE;
    }

    at_client_response_pattern_regist(&sg_socket_recv_pattern);
    // at_client_response_pattern_regist(&sg_socket_close_pattern);

    if (NULL == sg_socket[fd].mutex) {
        tal_mutex_create_init(&sg_socket[fd].mutex);
        if (sg_socket[fd].mutex == NULL) {
            PR_ERR("Failed to create mutex for socket fd: %d", fd);
        }
    }
#endif
    return fd;
}

/**
 * @brief Create a IPv6 tcp/udp socket
 *
 * @param[in] type: protocol type, tcp or udp
 *
 * @note This API is used for creating a tcp/udp socket.
 *
 * @return file descriptor
 */
int tkl_net_socket_create_v6(const TUYA_PROTOCOL_TYPE_E type)
{
    PR_DEBUG("--> [T] Creating IPv6 socket of type: %d", type);

    return -1;
}

/**
 * @brief Connect to network
 *
 * @param[in] fd: file descriptor
 * @param[in] addr: address information of server
 * @param[in] port: port information of server
 *
 * @note This API is used for connecting to network.
 *
 * @return 0 on success. Others on error, please refer to the error no of the target system
 */
TUYA_ERRNO tkl_net_connect(const int fd, const TUYA_IP_ADDR_T addr, const uint16_t port)
{
    OPERATE_RET rt = OPRT_OK;
    char *addr_str = tkl_net_addr2str(addr);

    PR_DEBUG("Connecting to %s:%d", addr_str, port);

#if 0
    int ret = at_vendor_ml307_socket_connect(fd, addr_str, port);
    if (ret != 0) {
        PR_ERR("AT vendor socket connect failed: %d", ret);
        return UNW_FAIL;
    }
#else
    if (fd >= TUYA_FD_MAX_COUNT || fd < 0) {
        PR_ERR("Invalid socket fd: %d", fd);
        return UNW_EINVAL;
    }

    if (sg_socket[fd].is_used == 0) {
        PR_ERR("Socket fd %d is not in use", fd);
        return UNW_EINVAL;
    }

    strncpy(sg_socket[fd].ip_addr, addr_str, sizeof(sg_socket[fd].ip_addr) - 1);
    sg_socket[fd].port = port;
    // sg_socket[fd].is_connected = TRUE; // Mark as connected

    // "AT+MIPOPEN=0,\"TCP\",\"47.103.71.77\",443\r";
    char tmp_buf[64] = {0};
    snprintf(tmp_buf, sizeof(tmp_buf), "AT+MIPOPEN=%d,\"%s\",\"%s\",%d\r", fd, GET_PROTOCOL_TYPE(sg_socket[fd].type),
             sg_socket[fd].ip_addr, sg_socket[fd].port);
    AT_LINE_T *line = NULL;
    uint32_t line_num = 0;
    rt = at_client_send(tmp_buf, strlen(tmp_buf), 1000, &line, &line_num);
    if (rt != OPRT_OK) {
        PR_ERR("Failed to send MIPOPEN command: %d", rt);
        return UNW_FAIL;
    }
    PR_DEBUG("[%s] Received %d lines after sending command: %s", __func__, line_num, tmp_buf);
    if (strstr(line->data, "OK") == NULL) {
        PR_ERR("Failed to open socket connection: %s", line->data);
        at_client_free_lines(line);
        return UNW_FAIL;
    }
    at_client_free_lines(line);
    at_client_get_one_line(&line);
    memset(tmp_buf, 0, sizeof(tmp_buf));
    // +MIPOPEN: 0,0
    snprintf(tmp_buf, sizeof(tmp_buf), "+MIPOPEN: %d,", fd);
    char *result = strstr(line->data, tmp_buf);
    if (result == NULL) {
        PR_ERR("Failed to get MIPOPEN response: %s", line->data);
        at_client_free_lines(line);
        return UNW_FAIL;
    }
    if (strstr(result, "0") == NULL) {
        // 0#建立成功。
        PR_ERR("Socket connection failed: %s", result);
        at_client_free_lines(line);
        return UNW_FAIL;
    } else {
        sg_socket[fd].is_connected = TRUE; // Mark as connected
        PR_DEBUG("Socket connection established successfully: %s", result);
    }
    at_client_free_lines(line);

    // Set send/recv format to HEX
    memset(tmp_buf, 0, sizeof(tmp_buf));
    snprintf(tmp_buf, sizeof(tmp_buf), "AT+MIPCFG=\"encoding\",%d,%d,%d\r", fd, 1, 1);
    rt = at_client_send(tmp_buf, strlen(tmp_buf), 1000, &line, &line_num);
    if (rt != OPRT_OK) {
        PR_ERR("Failed to set encoding mode: %d", rt);
        return UNW_FAIL;
    }
    PR_DEBUG("[%s] Received %d lines after sending command: %s", __func__, line_num, tmp_buf);
    if (strstr(line->data, "OK") == NULL) {
        PR_ERR("Failed to set encoding mode: %s", line->data);
        at_client_free_lines(line);
        return UNW_FAIL;
    }
    PR_DEBUG("Encoding mode set successfully: %s", line->data);
    at_client_free_lines(line);

#endif
    return UNW_SUCCESS;
}

/**
 * @brief Connect to network with raw data
 *
 * @param[in] fd: file descriptor
 * @param[in] p_socket: raw socket data
 * @param[in] len: data lenth
 *
 * @note This API is used for connecting to network with raw data.
 *
 * @return 0 on success. Others on error, please refer to the error no of the target system
 */
TUYA_ERRNO tkl_net_connect_raw(const int fd, void *p_socket_addr, const int len)
{
    PR_DEBUG("--> [T] Connecting to raw socket with fd: %d, len: %d", fd, len);

    return UNW_SUCCESS;
}

/**
 * @brief Bind to network
 *
 * @param[in] fd: file descriptor
 * @param[in] addr: address information of server
 * @param[in] port: port information of server
 *
 * @note This API is used for binding to network.
 *
 * @return 0 on success. Others on error, please refer to the error no of the target system
 */
TUYA_ERRNO tkl_net_bind(const int fd, const TUYA_IP_ADDR_T addr, const uint16_t port)
{
    PR_DEBUG("--> [T] Binding to %s:%d", tkl_net_addr2str(addr), port);

    return UNW_SUCCESS;
}

/**
 * @brief Listen to network
 *
 * @param[in] fd: file descriptor
 * @param[in] backlog: max count of backlog connection
 *
 * @note This API is used for listening to network.
 *
 * @return 0 on success. Others on error, please refer to the error no of the target system
 */
TUYA_ERRNO tkl_net_listen(const int fd, const int backlog)
{
    PR_DEBUG("--> [T] Listening on fd: %d with backlog: %d", fd, backlog);

    return UNW_SUCCESS;
}

/**
 * @brief Listen to network
 *
 * @param[in] fd: file descriptor
 * @param[out] addr: the accept ip addr
 * @param[out] port: the accept port number
 *
 * @note This API is used for listening to network.
 *
 * @return 0 on success. Others on error, please refer to the error no of the target system
 */
TUYA_ERRNO tkl_net_accept(const int fd, TUYA_IP_ADDR_T *addr, uint16_t *port)
{
    PR_DEBUG("--> [T] Accepting on fd: %d", fd);

    return UNW_SUCCESS;
}

/**
 * @brief Send data to network
 *
 * @param[in] fd: file descriptor
 * @param[in] buf: send data buffer
 * @param[in] nbytes: buffer lenth
 *
 * @note This API is used for sending data to network
 *
 * @return >0 on num of send, <0 please refer to the error no of the target system
 */
TUYA_ERRNO tkl_net_send(const int fd, const void *buf, const uint32_t nbytes)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("--> [T] Sending data on fd: %d, size: %u", fd, nbytes);

    TUYA_CHECK_NULL_RETURN(buf, UNW_EINVAL);

#if 0
    at_vendor_ml307_send(fd, buf, nbytes);

    int written_len = nbytes; // Placeholder for actual send length, replace with actual implementation
    return written_len;
#else
    if (fd >= TUYA_FD_MAX_COUNT || fd < 0) {
        PR_ERR("Invalid socket fd: %d", fd);
        return UNW_EINVAL;
    }

    if (sg_socket[fd].is_used == 0 || !sg_socket[fd].is_connected) {
        PR_ERR("Socket fd %d is not in use", fd);
        return UNW_EINVAL;
    }

    // malloc send buffer
    // AT+MIPSEND=0,len,"xxxxxxxxxxxxxxxxxxxxx"\r
    uint32_t malloc_len = nbytes * 2 + 64;
    uint32_t offset = 0;
    char *send_cmd = tal_malloc(malloc_len);
    if (send_cmd == NULL) {
        PR_ERR("Failed to allocate memory for send command");
        return UNW_ENOMEM;
    }
    memset(send_cmd, 0, malloc_len);
    offset += snprintf(send_cmd, malloc_len, "AT+MIPSEND=%d,%d,\"", fd, nbytes);
    for (uint32_t i = 0; i < nbytes; i++) {
        offset += snprintf(send_cmd + offset, malloc_len - offset, "%02X", ((uint8_t *)buf)[i]);
    }
    offset += snprintf(send_cmd + offset, malloc_len - offset, "\"\r");
    // PR_DEBUG("Sending command: %s", send_cmd);
    AT_LINE_T *line = NULL;
    uint32_t line_num = 0;
    rt = at_client_send(send_cmd, strlen(send_cmd), 1000, &line, &line_num);
    if (rt != OPRT_OK) {
        PR_ERR("Failed to send MIPOPEN command: %d", rt);
        return UNW_FAIL;
    }
    PR_DEBUG("[%s] Received %d lines after sending command: %s", __func__, line_num, send_cmd);
    if (strstr((line->next)->data, "OK") == NULL) {
        PR_ERR("Failed to send data: %s", line->data);
        at_client_lines_dump(line);
        at_client_free_lines(line);
        tal_free(send_cmd);
        return UNW_FAIL;
    }
    PR_DEBUG("Data sent successfully: %s", line->data);
    at_client_free_lines(line);
    tal_free(send_cmd);

    return nbytes; // Return the number of bytes sent

#endif
}

/**
 * @brief Send data to specified server
 *
 * @param[in] fd: file descriptor
 * @param[in] buf: send data buffer
 * @param[in] nbytes: buffer lenth
 * @param[in] addr: address information of server
 * @param[in] port: port information of server
 *
 * @note This API is used for sending data to network
 *
 * @return >0 on num of send, <0 please refer to the error no of the target system
 */
TUYA_ERRNO tkl_net_send_to(const int fd, const void *buf, const uint32_t nbytes, const TUYA_IP_ADDR_T addr,
                           const uint16_t port)
{
    PR_DEBUG("--> [T] Sending data to %s:%d on fd: %d, size: %u", tkl_net_addr2str(addr), port, fd, nbytes);

    return UNW_SUCCESS;
}

/**
 * @brief Receive data from network
 *
 * @param[in] fd: file descriptor
 * @param[in] buf: receive data buffer
 * @param[in] nbytes: buffer lenth
 *
 * @note This API is used for receiving data from network
 *
 * @return >0 on num of recv, <0 please refer to the error no of the target system
 */
TUYA_ERRNO tkl_net_recv(const int fd, void *buf, const uint32_t nbytes)
{
    PR_DEBUG("--> [T] Receiving data on fd: %d, size: %u, cur_used_size: %u", fd, nbytes, sg_socket[fd].recv_used);
#if 0
    int rt_len = at_vendor_ml307_read(fd, buf, nbytes);
    return rt_len;
#else
    if (fd >= TUYA_FD_MAX_COUNT || fd < 0) {
        PR_ERR("Invalid socket fd: %d", fd);
        return UNW_EINVAL;
    }

    if (sg_socket[fd].is_used == 0 || !sg_socket[fd].is_connected) {
        PR_ERR("Socket fd %d is not in use", fd);
        return UNW_EINVAL;
    }

    uint32_t time_cnt = 0;
    do {
        if (sg_socket[fd].recv_used >= nbytes) {
            break; // Enough data available
        }

        tal_system_sleep(100); // Sleep for 100 ms
        time_cnt++;
    } while (100 * time_cnt < sg_socket[fd].recv_timeout);

    tal_mutex_lock(sg_socket[fd].mutex);
    if (sg_socket[fd].recv == NULL || sg_socket[fd].recv_used == 0) {
        tal_mutex_unlock(sg_socket[fd].mutex);
        PR_ERR("No data available to read on fd: %d", fd);
        return UNW_EAGAIN; // No data available
    }

    uint32_t copy_len = (nbytes < sg_socket[fd].recv_used) ? nbytes : sg_socket[fd].recv_used;
    memcpy(buf, sg_socket[fd].recv, copy_len);
    sg_socket[fd].recv_used -= copy_len;
    if (sg_socket[fd].recv_used == 0) {
        tal_free(sg_socket[fd].recv); // Free the buffer if no data left
        sg_socket[fd].recv = NULL;    // Set to NULL to avoid dangling pointer
    } else {
        // Shift remaining data to the start of the buffer
        memmove(sg_socket[fd].recv, sg_socket[fd].recv + copy_len, sg_socket[fd].recv_used);
    }

    tal_mutex_unlock(sg_socket[fd].mutex);

    return copy_len;

#endif
}

/**
 * @brief Receive data from network with need size
 *
 * @param[in] fd: file descriptor
 * @param[in] buf: receive data buffer
 * @param[in] nbytes: buffer lenth
 * @param[in] nd_size: the need size
 *
 * @note This API is used for receiving data from network with need size
 *
 * @return >0 on success. Others on error
 */
int tkl_net_recv_nd_size(const int fd, void *buf, const uint32_t buf_size, const uint32_t nd_size)
{
    PR_DEBUG("--> [T] Receiving data on fd: %d, size: %u, need size: %u", fd, buf_size, nd_size);

    return 0;
}

/**
 * @brief Receive data from specified server
 *
 * @param[in] fd: file descriptor
 * @param[in] buf: receive data buffer
 * @param[in] nbytes: buffer lenth
 * @param[in] addr: address information of server
 * @param[in] port: port information of server
 *
 * @note This API is used for receiving data from specified server
 *
 * @return >0 on num of recv, <0 please refer to the error no of the target system
 */
TUYA_ERRNO tkl_net_recvfrom(const int fd, void *buf, const uint32_t nbytes, TUYA_IP_ADDR_T *addr, uint16_t *port)
{
    PR_DEBUG("--> [T] Receiving data from %s:%d on fd: %d, size: %u", tkl_net_addr2str(*addr), *port, fd, nbytes);

    return UNW_SUCCESS;
}

/**
 * @brief Get address information by domain
 *
 * @param[in] domain: domain information
 * @param[in] addr: address information
 *
 * @note This API is used for getting address information by domain.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_gethostbyname(const char *domain, TUYA_IP_ADDR_T *addr)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(domain, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(addr, OPRT_INVALID_PARM);
#if 0
    at_vendor_ml307_gethostbyname(domain, addr);
#else
    // AT+MDNSGIP="h6.iot-dns.com"
    char *tmp_buf = NULL;
    uint32_t malloc_len = strlen(domain) + 16;
    tmp_buf = tal_malloc(malloc_len);
    if (tmp_buf == NULL) {
        PR_ERR("Failed to allocate memory for domain buffer");
        return OPRT_MALLOC_FAILED;
    }
    memset(tmp_buf, 0, malloc_len);
    snprintf(tmp_buf, malloc_len, "AT+MDNSGIP=\"%s\"\r", domain);
    PR_DEBUG("Sending command: %s", tmp_buf);
    AT_LINE_T *line = NULL;
    uint32_t line_num = 0;
    rt = at_client_send(tmp_buf, strlen(tmp_buf), 1000, &line, &line_num);
    tal_free(tmp_buf);
    if (rt != OPRT_OK) {
        PR_ERR("Failed to send MDNSGIP command: %d", rt);
        return OPRT_COM_ERROR;
    }
    PR_DEBUG("[%s] Received %d lines after sending command: %s", __func__, line_num, tmp_buf);
    if (strstr(line->data, "OK") == NULL) {
        PR_ERR("Failed to get host by name: %s", line->data);
        at_client_free_lines(line);
        return OPRT_COM_ERROR;
    }
    PR_DEBUG("Host by name retrieved successfully: %s", line->data);
    at_client_free_lines(line);
    line = NULL;

    at_client_get_one_line(&line);
    if (line == NULL) {
        PR_ERR("Failed to get response line for MDNSGIP");
        return OPRT_COM_ERROR;
    }

    // Parse the response to extract the IP address
    // Response format: +MDNSGIP: "domain","ipv6","ipv4","ipv4",...
    char *ip_start = strstr(line->data, domain);
    if (ip_start == NULL) {
        PR_ERR("Failed to find domain in response: %s", line->data);
        return OPRT_COM_ERROR;
    }

    // Skip domain part and find first IPv4 address (skip IPv6 if present)
    ip_start = strchr(ip_start, ','); // Find first comma after domain
    if (ip_start == NULL) {
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
        PR_ERR("Failed to extract IP address from response: %s", line->data);
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

#endif
    return rt;
}

/**
 * @brief Bind to network with specified ip
 *
 * @param[in] fd: file descriptor
 * @param[in] ip: ip address
 *
 * @note This API is used for binding to network with specified ip.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_socket_bind(const int fd, const char *ip)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("--> [T] Binding to %s on fd: %d", ip, fd);

    return rt;
}

/**
 * @brief Set socket fd close mode
 *
 * @param[in] fd: file descriptor
 *
 * @note This API is used for setting socket fd close mode, the socket fd will not be closed in child processes
 * generated by fork calls.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_set_cloexec(const int fd)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("--> [T] Setting close-on-exec for fd: %d", fd);

    return rt;
}

/**
 * @brief Get ip address by socket fd
 *
 * @param[in] fd: file descriptor
 * @param[out] addr: ip address
 *
 * @note This API is used for getting ip address by socket fd.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_get_socket_ip(const int fd, TUYA_IP_ADDR_T *addr)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("--> [T] Getting socket IP for fd: %d", fd);

    return rt;
}

/**
 * @brief Change ip string to address
 *
 * @param[in] ip_str: ip string
 *
 * @note This API is used to change ip string to address.
 *
 * @return ip address
 */
TUYA_IP_ADDR_T tkl_net_str2addr(const char *ip_str)
{
    TUYA_IP_ADDR_T ip_addr = 0;

    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);
    if (inet_pton(AF_INET, ip_str, &(sa.sin_addr)) <= 0) {
        PR_ERR("Invalid IP address format");
        return 0;
    }
    ip_addr = ntohl(sa.sin_addr.s_addr);

    PR_DEBUG("Converted IP string '%s' to address: %u", ip_str, ip_addr);

    return ip_addr;
}

/**
 * @brief Change ip address to string
 *
 * @param[in] ipaddr: ip address
 *
 * @note This API is used to change ip address(in host byte order) to string(in IPv4 numbers-and-dots(xx.xx.xx.xx)
 * notion).
 *
 * @return ip string
 */
char *tkl_net_addr2str(const TUYA_IP_ADDR_T ipaddr)
{
    static char str[INET_ADDRSTRLEN];
    struct in_addr addr;
    addr.s_addr = htonl(ipaddr);

    const char *result = inet_ntop(AF_INET, &addr, str, INET_ADDRSTRLEN);
    if (result == NULL) {
        // 如果转换失败，返回 "0.0.0.0"
        snprintf(str, sizeof(str), "0.0.0.0");
    }

    return str;
}

/**
 * @brief Set socket options
 *
 * @param[in] fd: file descriptor
 * @param[in] level: setting level
 * @param[in] optname: the name of the option
 * @param[in] optval: the value of option
 * @param[in] optlen: the length of the option value
 *
 * @note This API is used for setting socket options.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_setsockopt(const int fd, const TUYA_OPT_LEVEL level, const TUYA_OPT_NAME optname,
                               const void *optval, const int optlen)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("--> [T] Setting socket options on fd: %d, level: %d, optname: %d, optlen: %d", fd, level, optname,
             optlen);

    return rt;
}

/**
 * @brief Get socket options
 *
 * @param[in] fd: file descriptor
 * @param[in] level: getting level
 * @param[in] optname: the name of the option
 * @param[out] optval: the value of option
 * @param[out] optlen: the length of the option value
 *
 * @note This API is used for getting socket options.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_getsockopt(const int fd, const TUYA_OPT_LEVEL level, const TUYA_OPT_NAME optname, void *optval,
                               int *optlen)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("--> [T] Getting socket options on fd: %d, level: %d, optname: %d", fd, level, optname);

    return rt;
}

/**
 * @brief Set timeout option of socket fd
 *
 * @param[in] fd: file descriptor
 * @param[in] ms_timeout: timeout in ms
 * @param[in] type: transfer type, receive or send
 *
 * @note This API is used for setting timeout option of socket fd.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_set_timeout(const int fd, const int ms_timeout, const TUYA_TRANS_TYPE_E type)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("--> [T] Setting timeout for fd: %d, ms_timeout: %d, type: %d", fd, ms_timeout, type);

    if (fd < 0 || fd >= TUYA_FD_MAX_COUNT) {
        PR_ERR("Invalid file descriptor: %d", fd);
        return UNW_FAIL;
    }

    if (sg_socket[fd].is_used == 0) {
        PR_ERR("Socket fd: %d is not in use", fd);
        return UNW_FAIL;
    }

    tal_mutex_lock(sg_socket[fd].mutex);

    sg_socket[fd].recv_timeout = ms_timeout;
    sg_socket[fd].send_timeout = ms_timeout;

    tal_mutex_unlock(sg_socket[fd].mutex);

    return rt;
}

/**
 * @brief Set buffer_size option of socket fd
 *
 * @param[in] fd: file descriptor
 * @param[in] buf_size: buffer size in byte
 * @param[in] type: transfer type, receive or send
 *
 * @note This API is used for setting buffer_size option of socket fd.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_set_bufsize(const int fd, const int buf_size, const TUYA_TRANS_TYPE_E type)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("--> [T] Setting buffer size for fd: %d, buf_size: %d, type: %d", fd, buf_size, type);

    return rt;
}

/**
 * @brief Enable reuse option of socket fd
 *
 * @param[in] fd: file descriptor
 *
 * @note This API is used to enable reuse option of socket fd.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_set_reuse(const int fd)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("--> [T] Enabling reuse option for fd: %d", fd);

    return rt;
}

/**
 * @brief Disable nagle option of socket fd
 *
 * @param[in] fd: file descriptor
 *
 * @note This API is used to disable nagle option of socket fd.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_disable_nagle(const int fd)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("--> [T] Disabling Nagle option for fd: %d", fd);

    return rt;
}

/**
 * @brief Enable broadcast option of socket fd
 *
 * @param[in] fd: file descriptor
 *
 * @note This API is used to enable broadcast option of socket fd.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_set_broadcast(const int fd)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("--> [T] Enabling broadcast option for fd: %d", fd);

    return rt;
}

/**
 * @brief Set keepalive option of socket fd to monitor the connection
 *
 * @param[in] fd: file descriptor
 * @param[in] alive: keepalive option, enable or disable option
 * @param[in] idle: keep idle option, if the connection has no data exchange with the idle time(in seconds), start
 * probe.
 * @param[in] intr: keep interval option, the probe time interval.
 * @param[in] cnt: keep count option, probe count.
 *
 * @note This API is used to set keepalive option of socket fd to monitor the connection.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_set_keepalive(int fd, const BOOL_T alive, const uint32_t idle, const uint32_t intr,
                                  const uint32_t cnt)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("--> [T] Setting keepalive for fd: %d, alive: %d, idle: %u, intr: %u, cnt: %u", fd, alive, idle, intr,
             cnt);

    return rt;
}

/**
 * @brief Get socket name
 *
 * @param[in] fd: file descriptor
 * @param[out] addr: ip address
 * @param[out] port: port information
 *
 * @note This API is used to Get the current name for the specified socket
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_getsockname(int fd, TUYA_IP_ADDR_T *addr, uint16_t *port)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("--> [T] Getting socket name for fd: %d", fd);

    return rt;
}

/**
 * @brief Get name of connected peer socket
 *
 * @param[in] fd: file descriptor
 * @param[out] addr: ip address
 * @param[out] port: port information
 *
 * @note This API is used to Get the name of connected peer socket.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_getpeername(int fd, TUYA_IP_ADDR_T *addr, uint16_t *port)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("--> [T] Getting peer name for fd: %d", fd);

    return rt;
}

/**
 * @brief Set the system hostname
 *
 * @param[in] hostname: hostname to set
 *
 * @note This API is used to set the system hostname.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_sethostname(const char *hostname)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("--> [T] Setting hostname: %s", hostname);

    if (hostname == NULL) {
        return OPRT_INVALID_PARM;
    }

    return rt;
}

/**
 * @brief get netif by index
 *
 * @param[in]       net_if_idx    the num of netif index
 * @return  NULL: get netif fail   other: the point of netif
 */
// void *tkl_net_get_netif_by_index(const TUYA_NETIF_TYPE_E net_if_idx)
// {
//     void *netif = NULL;

//     return netif;
// }

/**
 * @brief Check ipv4/v6
 *
 * @param[in] has_ipv4: ipv4 is ready
 * @param[in] has_ipv6: ipv6 is ready
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_net_check_system_ipv4_ipv6(BOOL_T *has_ipv4, BOOL_T *has_ipv6)
{
    OPERATE_RET rt = OPRT_OK;
    return rt;
}