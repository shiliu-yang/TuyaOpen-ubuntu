/**
 * @file at_client.c
 * @brief at_client module is used to
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "at_client.h"
#include "at_parser.h"

#include "at_vendor_ml307.h"

#include "tdl_transport_manage.h"

#include "tal_api.h"
#include "tkl_queue.h"

/***********************************************************
************************macro define************************
***********************************************************/
#define AT_CLIENT_RECV_BUFFER_SIZE (5 * 1024) // Size of the receive buffer

#define AT_CLIENT_STATUS_CHANGE(new_status)                                                                            \
    do {                                                                                                               \
        PR_DEBUG("AT client status changed: [%s] --> [%s]", AT_CLIENT_STATUS_STR[sg_at_client.status],                 \
                 AT_CLIENT_STATUS_STR[new_status]);                                                                    \
        sg_at_client.status = new_status;                                                                              \
    } while (0)

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef uint8_t AT_CLIENT_STATUS_T;
#define AT_CLIENT_STATUS_IDLE       0x00 // Client uninitialized
#define AT_CLIENT_STATUS_SENDING    0x01 // Client sending commands
#define AT_CLIENT_STATUS_WAITING    0x02 // Client waiting for response
#define AT_CLIENT_STATUS_PROCESSING 0x03 // Client processing response
#define AT_CLIENT_STATUS_COMPLETED  0x04 // Client completed command
#define AT_CLIENT_STATUS_ERROR      0x05 // Client encountered an error
#define AT_CLIENT_STATUS_TIMEOUT    0x06 // Client command timed out

typedef struct {
    char *cmd;           // 命令名称
    uint32_t cmd_length; // 命令长度

    uint32_t send_time;  // 发送时间
    uint32_t timeout_ms; // 超时时间

    // 回调函数
    ON_INTERMEDIATE_CB on_intermediate; // 中间数据回调函数
} AT_CMD_CONTEXT_T;

typedef struct {
    AT_LINE_T *line;
    uint32_t num;
} AT_RSP_CONTEXT_T;

typedef struct {
    THREAD_HANDLE thread_hdl;
    MUTEX_HANDLE mutex;
    QUEUE_HANDLE rsp_queue;

    AT_CLIENT_STATUS_T status;

    TDL_TRANSPORT_HANDLE transport_hdl; // Handle for transport layer
    AT_PARSER_HANDLE parser_hdl;        // Handle for AT parser

    AT_CMD_CONTEXT_T cmd_context; // 当前命令上下文
    OPERATE_RET send_rt;          // 发送结果
    char *recv_buffer;            // 接收缓冲区
    uint32_t recv_buffer_size;    // 接收缓冲区大小
    uint32_t recv_buffer_used;    // 已使用的接收缓冲区大小
} AT_CLIENT_T;

/***********************************************************
********************function declaration********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/
static char *AT_CLIENT_STATUS_STR[] = {"IDLE", "SENDING", "WAITING", "PROCESSING", "COMPLETED", "ERROR", "TIMEOUT"};

AT_CLIENT_T sg_at_client = {
    .thread_hdl = NULL,
    .mutex = NULL,
    .status = AT_CLIENT_STATUS_IDLE,
    .transport_hdl = NULL,
};

/***********************************************************
***********************function define**********************
***********************************************************/

static void __at_client_thread(void *arg)
{
    OPERATE_RET rt = OPRT_OK;
    uint32_t delay_ms = 100; // Delay in milliseconds
    uint32_t cnt = 0;

    for (;;) {
        switch (sg_at_client.status) {
        case AT_CLIENT_STATUS_IDLE: {
            if (delay_ms != 100) {
                delay_ms = 100;
            }
            uint32_t available = tdl_transport_available(sg_at_client.transport_hdl);
            if (available > 0) {
                // If there is data available, change status to SENDING
                AT_CLIENT_STATUS_CHANGE(AT_CLIENT_STATUS_WAITING);
            }
        } break;
        case AT_CLIENT_STATUS_SENDING: {
            rt = tdl_transport_send(sg_at_client.transport_hdl, sg_at_client.cmd_context.cmd,
                                    sg_at_client.cmd_context.cmd_length);
            if (rt != OPRT_OK) {
                PR_ERR("Failed to send command: %s, error: %d", sg_at_client.cmd_context.cmd, rt);
                sg_at_client.send_rt = rt;
                AT_CLIENT_STATUS_CHANGE(AT_CLIENT_STATUS_ERROR);
                break;
            }

            memset(sg_at_client.recv_buffer, 0, sg_at_client.recv_buffer_size);
            sg_at_client.recv_buffer_used = 0;

            AT_CLIENT_STATUS_CHANGE(AT_CLIENT_STATUS_WAITING);
        } break;
        case AT_CLIENT_STATUS_WAITING: {
            // Wait for response from transport layer
            // If response received, change status to PROCESSING
            if (delay_ms != 20) {
                delay_ms = 20;
            }

            uint32_t available = tdl_transport_available(sg_at_client.transport_hdl);
            if (available > 0) {
                uint32_t read_len = tdl_transport_read(sg_at_client.transport_hdl,
                                                       sg_at_client.recv_buffer + sg_at_client.recv_buffer_used,
                                                       sg_at_client.recv_buffer_size - sg_at_client.recv_buffer_used);
                if (read_len > 0) {
                    sg_at_client.recv_buffer_used += read_len;
                    PR_DEBUG("Received %d bytes, total: %d bytes", read_len, sg_at_client.recv_buffer_used);
                }
            } else {
                break;
            }

            if (sg_at_client.recv_buffer_used < 4) {
                break;
            }

            // add line
            char *next_line =
                at_parser_line_input(sg_at_client.parser_hdl, sg_at_client.recv_buffer, sg_at_client.recv_buffer_used);
            if (NULL == next_line || next_line == sg_at_client.recv_buffer) {
                // No complete line found, continue waiting
                break;
            } else {
                // Process the complete line
                PR_DEBUG("Processing line: %s", next_line);
                memmove(sg_at_client.recv_buffer, next_line,
                        sg_at_client.recv_buffer_used - (next_line - sg_at_client.recv_buffer));
                sg_at_client.recv_buffer_used -= (next_line - sg_at_client.recv_buffer);
                AT_CLIENT_STATUS_CHANGE(AT_CLIENT_STATUS_PROCESSING);
                continue;
            }
        } break;
        case AT_CLIENT_STATUS_PROCESSING: {
            // Process the response
            uint32_t line_num = at_parser_get_line_num(sg_at_client.parser_hdl);

            if (line_num == 0) {
                PR_ERR("No lines to process");
                AT_CLIENT_STATUS_CHANGE(AT_CLIENT_STATUS_WAITING);
                break;
            }

            AT_RSP_CONTEXT_T rsp_ctx;
            rsp_ctx.line = at_parser_get_line(sg_at_client.parser_hdl, 0);
            for (uint32_t i = 0; i < line_num; i++) {
                AT_LINE_T *line = at_parser_get_line(sg_at_client.parser_hdl, i);
                if (line == NULL) {
                    PR_ERR("Failed to get line %d", i);
                    AT_CLIENT_STATUS_CHANGE(AT_CLIENT_STATUS_WAITING);
                }

                // Match patterns
                AT_RESPONSE_PATTERN_T *matched_pattern = at_parser_pattern_match(sg_at_client.parser_hdl, line);
                if (matched_pattern) {
                    if (matched_pattern->response_type == AT_RESPONSE_TYPE_URC) {
                        // URC response, handle it
                        PR_DEBUG("URC matched: %s", matched_pattern->pattern);
                        at_parser_split_lines(sg_at_client.parser_hdl, line, 1);
                        if (matched_pattern->callback) {
                            matched_pattern->callback(line->data, line->length, matched_pattern->user_data);
                        }
                    }

                    if (matched_pattern->is_final) {
                        PR_DEBUG("Matched pattern: %s", matched_pattern->pattern);
                        rsp_ctx.num = i + 1; // Line number is 1-based

                        at_parser_split_lines(sg_at_client.parser_hdl, rsp_ctx.line, rsp_ctx.num);
                        PR_DEBUG("Final response matched, line number: %d", rsp_ctx.num);

                        // Add response context to queue
                        tal_queue_post(sg_at_client.rsp_queue, &rsp_ctx, TKL_QUEUE_WAIT_FROEVER);
                        AT_CLIENT_STATUS_CHANGE(AT_CLIENT_STATUS_COMPLETED);
                        break;
                    }
                } else {
                    AT_CLIENT_STATUS_CHANGE(AT_CLIENT_STATUS_WAITING);
                }
            }
        } break;
        case AT_CLIENT_STATUS_COMPLETED: {
            // Command completed successfully
            PR_DEBUG("Command completed successfully: %s", sg_at_client.cmd_context.cmd);
            AT_CLIENT_STATUS_CHANGE(AT_CLIENT_STATUS_IDLE);
            sg_at_client.send_rt = OPRT_OK;
            // Reset command context
            memset(&sg_at_client.cmd_context, 0, sizeof(AT_CMD_CONTEXT_T));
            // Reset receive buffer
            memset(sg_at_client.recv_buffer, 0, sg_at_client.recv_buffer_size);
            sg_at_client.recv_buffer_used = 0;
            delay_ms = 100; // Reset delay to 100ms after completion
        } break;
        case AT_CLIENT_STATUS_ERROR: {
            // Handle error
            PR_ERR("AT client encountered an error: %d", sg_at_client.send_rt);
            AT_CLIENT_STATUS_CHANGE(AT_CLIENT_STATUS_IDLE);
        } break;
        default:
            break;
        }

        tal_system_sleep(delay_ms);
    }
}

OPERATE_RET at_client_init(char *transport_name)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(transport_name, OPRT_INVALID_PARM);

    TUYA_CALL_ERR_RETURN(tdl_transport_find(transport_name, &sg_at_client.transport_hdl));
    TUYA_CALL_ERR_RETURN(tdl_transport_open(sg_at_client.transport_hdl));

    // create AT parser
    AT_PARSER_CFG_T parser_cfg = {
        .line_ending = LINE_ENDING_CRLF // Use CRLF as line ending
    };
    TUYA_CALL_ERR_GOTO(at_parser_init(&sg_at_client.parser_hdl, &parser_cfg), __ERR);

    // TODO:
    TUYA_CALL_ERR_GOTO(at_vendor_ml307_parse_register(sg_at_client.parser_hdl), __ERR);

    // Create receive buffer
    sg_at_client.recv_buffer_size = AT_CLIENT_RECV_BUFFER_SIZE;
    sg_at_client.recv_buffer = (char *)tal_malloc(AT_CLIENT_RECV_BUFFER_SIZE);
    TUYA_CHECK_NULL_GOTO(sg_at_client.recv_buffer, __ERR);
    memset(sg_at_client.recv_buffer, 0, sg_at_client.recv_buffer_size);

    sg_at_client.status = AT_CLIENT_STATUS_IDLE;

    TUYA_CALL_ERR_GOTO(tal_mutex_create_init(&sg_at_client.mutex), __ERR);
    TUYA_CALL_ERR_GOTO(tal_queue_create_init(&sg_at_client.rsp_queue, sizeof(AT_RSP_CONTEXT_T), 10), __ERR);

    // Create and start the AT client thread
    THREAD_CFG_T thrd_param = {4096, 4, "at_client"};
    rt = tal_thread_create_and_start(&sg_at_client.thread_hdl, NULL, NULL, __at_client_thread, NULL, &thrd_param);
    if (rt != OPRT_OK) {
        PR_ERR("Failed to create AT client thread: %d", rt);
        goto __ERR;
    }

    return rt;
__ERR:

    if (sg_at_client.parser_hdl) {
        at_parser_deinit(sg_at_client.parser_hdl);
        sg_at_client.parser_hdl = NULL;
    }

    if (sg_at_client.mutex) {
        tal_mutex_release(sg_at_client.mutex);
        sg_at_client.mutex = NULL;
    }

    return rt;
}

OPERATE_RET at_client_send(char *cmd, uint32_t cmd_length, uint32_t timeout_ms, AT_LINE_T **line, uint32_t *line_num)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(cmd, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(sg_at_client.thread_hdl, OPRT_INVALID_PARM);

    tal_mutex_lock(sg_at_client.mutex);

    sg_at_client.send_rt = OPRT_OK;

    AT_CMD_CONTEXT_T *p_cmd = &sg_at_client.cmd_context;
    p_cmd->cmd = (char *)cmd;
    p_cmd->cmd_length = cmd_length;
    p_cmd->timeout_ms = timeout_ms;
    // p_cmd->on_intermediate = on_intermediate;

    AT_CLIENT_STATUS_CHANGE(AT_CLIENT_STATUS_SENDING);

    AT_RSP_CONTEXT_T rsp_ctx;
    tal_queue_fetch(sg_at_client.rsp_queue, &rsp_ctx, TKL_QUEUE_WAIT_FROEVER);
    *line = rsp_ctx.line;
    *line_num = rsp_ctx.num;

    tal_mutex_unlock(sg_at_client.mutex);

    return rt;
}

OPERATE_RET at_client_get_one_line(AT_LINE_T **line)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(line, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(sg_at_client.parser_hdl, OPRT_INVALID_PARM);

    tal_mutex_lock(sg_at_client.mutex);

    while (0 == at_parser_get_line_num(sg_at_client.parser_hdl)) {
        tal_system_sleep(50); // Wait for a line to be available
    }

    *line = at_parser_get_line(sg_at_client.parser_hdl, 0);
    if (*line == NULL) {
        PR_ERR("Failed to get line from AT parser");
        tal_mutex_unlock(sg_at_client.mutex);
        return OPRT_COM_ERROR;
    }
    at_parser_split_lines(sg_at_client.parser_hdl, *line, 1);
    (*line)->next = NULL; // Ensure the next pointer is NULL

    tal_mutex_unlock(sg_at_client.mutex);

    PR_DEBUG("at_client_get_one_line line: %.*s", (*line)->length, (*line)->data);

    return rt;
}
