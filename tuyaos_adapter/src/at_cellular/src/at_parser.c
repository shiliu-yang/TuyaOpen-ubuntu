/**
 * @file at_parser.c
 * @brief at_parser module is used to parse AT commands and responses.
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "at_parser.h"

#include "tal_api.h"

/***********************************************************
************************macro define************************
***********************************************************/
#define AT_PARSER_MAGIC (0x12345678)

#define RESPONSE_BUFFER_SIZE (32)
#define RESPONSE_BUFFER_NUM  (4)

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct at_line {
    struct at_line *next;

    uint32_t length;
    char *data;
} AT_LINE_T;

typedef struct {
    uint32_t magic; // Magic number for validation

    // line ending
    char line_ending[LINE_ENDING_MAX_LENGTH];

    AT_LINE_T *line_head; // Pointer to the head of the line list
    AT_LINE_T *line_tail; // Pointer to the tail of the line list
    uint32_t line_count;  // Count of lines processed
} AT_PARSER_T;
/***********************************************************
********************function declaration********************
***********************************************************/

OPERATE_RET at_parser_free_line(AT_LINE_T *line);

/***********************************************************
***********************variable define**********************
***********************************************************/

/***********************************************************
***********************function define**********************
***********************************************************/
OPERATE_RET at_parser_init(AT_PARSER_HANDLE *handle, AT_PARSER_CFG_T *cfg)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(handle, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(cfg, OPRT_INVALID_PARM);

    AT_PARSER_T *parser = (AT_PARSER_T *)tal_malloc(sizeof(AT_PARSER_T));
    TUYA_CHECK_NULL_RETURN(parser, OPRT_MALLOC_FAILED);
    memset(parser, 0, sizeof(AT_PARSER_T));

    parser->magic = AT_PARSER_MAGIC;
    strncpy(parser->line_ending, cfg->line_ending, LINE_ENDING_MAX_LENGTH - 1);
    parser->line_ending[LINE_ENDING_MAX_LENGTH - 1] = '\0';

    *handle = (AT_PARSER_HANDLE)parser;

    PR_DEBUG("AT parser initialized with line ending: %s", parser->line_ending);

    return rt;
}

// 命令注册
// OPERATE_RET at_parser_

OPERATE_RET at_parser_deinit(AT_PARSER_HANDLE handle)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(handle, OPRT_INVALID_PARM);

    AT_PARSER_T *parser = (AT_PARSER_T *)handle;

    if (parser->magic != AT_PARSER_MAGIC) {
        PR_ERR("Invalid AT parser magic number");
        return OPRT_INVALID_PARM;
    }

    tal_free(parser);

    return OPRT_OK;
}

OPERATE_RET at_parser_add_line(AT_PARSER_HANDLE handle, const char *line_data, uint32_t length)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(handle, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(line_data, OPRT_INVALID_PARM);

    AT_PARSER_T *parser = (AT_PARSER_T *)handle;

    if (parser->magic != AT_PARSER_MAGIC) {
        PR_ERR("Invalid AT parser magic number");
        return OPRT_INVALID_PARM;
    }

    AT_LINE_T *new_line = (AT_LINE_T *)tal_malloc(sizeof(AT_LINE_T));
    TUYA_CHECK_NULL_RETURN(new_line, OPRT_MALLOC_FAILED);
    memset(new_line, 0, sizeof(AT_LINE_T));

    new_line->data = (char *)tal_malloc(length + 1);
    if (NULL == new_line->data) {
        rt = OPRT_MALLOC_FAILED;
        goto __ERR;
    }
    memset(new_line->data, 0, length + 1);
    memcpy(new_line->data, line_data, length);

    new_line->length = length;
    new_line->next = NULL;

    if (NULL == parser->line_head && NULL == parser->line_tail) {
        parser->line_head = new_line;
        parser->line_tail = new_line;
    } else {
        parser->line_tail->next = new_line;
        parser->line_tail = new_line;
    }

    // PR_DEBUG("Added line: %s", new_line->data);
    PR_HEXDUMP_DEBUG("Added line", new_line->data, new_line->length);

    return OPRT_OK;

__ERR:
    at_parser_free_line(new_line);

    return rt;
}

OPERATE_RET at_parser_free_line(AT_LINE_T *line)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(line, OPRT_INVALID_PARM);

    if (line->data) {
        tal_free(line->data);
        line->data = NULL;
    }
    tal_free(line);
    line = NULL;

    return OPRT_OK;
}

OPERATE_RET at_parser_input(AT_PARSER_HANDLE handle, char *data, uint32_t length)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(handle, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(data, OPRT_INVALID_PARM);

    AT_PARSER_T *parser = (AT_PARSER_T *)handle;

    if (parser->magic != AT_PARSER_MAGIC) {
        PR_ERR("Invalid AT parser magic number");
        return OPRT_INVALID_PARM;
    }

    char *p_start = data;
    char *p_end = NULL;
    uint32_t offset = 0;

    do {
        p_end = strstr(p_start, parser->line_ending);
        if (p_end && p_end > p_start) {
            at_parser_add_line(handle, p_start, p_end - p_start);
            offset = p_end - data + strlen(parser->line_ending);
            p_start = data + offset;
        } else if (p_end == p_start) {
            p_start += strlen(parser->line_ending);
            offset += strlen(parser->line_ending);
        } else {
            break;
        }
    } while (1);

    return rt;
}
