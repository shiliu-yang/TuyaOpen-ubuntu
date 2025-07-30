/**
 * @file at_vendor_ml307.h
 * @brief at_vendor_ml307 module is used to
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __AT_VENDOR_ML307_H__
#define __AT_VENDOR_ML307_H__

#include "tuya_cloud_types.h"
#include "at_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/

// Response codes
#define AT_RESPONSE_OK          "OK\r\n"
#define AT_RESPONSE_ERROR       "ERROR\r\n"
#define AT_RESPONSE_ERROR_CME   "+CME ERROR: "
#define AT_RESPONSE_ERROR_CMS   "+CMS ERROR: "
#define AT_RESPONSE_ERROR_CIS   "+CIS ERROR: "

/***********************************************************
***********************typedef define***********************
***********************************************************/

/***********************************************************
********************function declaration********************
***********************************************************/

OPERATE_RET at_vendor_ml307_register(void);

OPERATE_RET at_vendor_ml307_socket_create(int *sock_fd, const TUYA_PROTOCOL_TYPE_E type);

OPERATE_RET at_vendor_ml307_socket_close(int fd);

OPERATE_RET at_vendor_ml307_socket_connect(int fd, const char *addr, const uint16_t port);

OPERATE_RET at_vendor_ml307_gethostbyname(const char *domain, TUYA_IP_ADDR_T *addr);

OPERATE_RET at_vendor_ml307_send(const int fd, const void *buf, const uint32_t nbytes);

OPERATE_RET at_vendor_ml307_read(const int fd, void *buf, const uint32_t nbytes);

OPERATE_RET at_vendor_ml307_parse_register(AT_PARSER_HANDLE handle);

#ifdef __cplusplus
}
#endif

#endif /* __AT_VENDOR_ML307_H__ */
