/**
 * @file at_client.h
 * @brief at_client module is used to
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __AT_CLIENT_H__
#define __AT_CLIENT_H__

#include "tuya_cloud_types.h"

#include "at_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef void (*ON_INTERMEDIATE_CB)(const char *line);

/***********************************************************
********************function declaration********************
***********************************************************/

OPERATE_RET at_client_init(char *transport_name);

OPERATE_RET at_client_send(char *cmd, uint32_t cmd_length, uint32_t timeout_ms, AT_LINE_T **line, uint32_t *line_num);

OPERATE_RET at_client_get_one_line(AT_LINE_T **line);

OPERATE_RET at_client_free_lines(AT_LINE_T *line);

OPERATE_RET at_client_lines_dump(AT_LINE_T *line);

#ifdef __cplusplus
}
#endif

#endif /* __AT_CLIENT_H__ */
