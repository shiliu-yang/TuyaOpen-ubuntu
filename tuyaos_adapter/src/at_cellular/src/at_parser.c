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

    // response pattern
    AT_RESPONSE_PATTERN_T *pattern;
    uint32_t pattern_count; // Count of response patterns registered
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

// 数据解析
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
    
    // 1. 行解析：将原始数据解析成完整的行
    rt = at_parser_line_input(handle, data, length);
    if (rt != OPRT_OK) {
        return rt;
    }
    
    // 2. 响应处理：对解析出的行进行模式匹配和处理
    rt = at_parser_process_lines(handle);
    
    return rt;
}

/**
 * @brief 处理已解析的行数据 - 响应匹配与处理层
 */
static OPERATE_RET at_parser_process_lines(AT_PARSER_HANDLE handle)
{
    OPERATE_RET rt = OPRT_OK;
    
    AT_PARSER_T *parser = (AT_PARSER_T *)handle;
    AT_LINE_T *current_line = parser->line_head;
    
    // 遍历所有未处理的行
    while (current_line != NULL) {
        AT_LINE_T *next_line = current_line->next;
        
        // 对每一行进行响应匹配
        at_response_pattern_t *matched_pattern = at_parser_match_response_pattern(parser, current_line->data);
        
        if (matched_pattern) {
            // 找到匹配的模式，进行响应处理
            at_parser_process_response(parser, current_line->data, matched_pattern);
        } else {
            // 未匹配的响应，记录或者作为未知响应处理
            PR_WARN("Unknown response: %s", current_line->data);
        }
        
        // 移除已处理的行
        at_parser_remove_line(parser, current_line);
        
        current_line = next_line;
    }
    
    return rt;
}

/**
 * @brief 响应模式匹配
 */
static at_response_pattern_t *at_parser_match_response_pattern(AT_PARSER_T *parser, const char *line)
{
    if (!parser->pattern || parser->pattern_count == 0) {
        return NULL;
    }
    
    // 计算行的哈希值用于快速匹配
    uint32_t line_hash = at_parser_compute_hash(line);
    
    // 遍历已注册的响应模式
    for (uint32_t i = 0; i < parser->pattern_count; i++) {
        at_response_pattern_t *pattern = &parser->pattern[i];
        
        // 快速哈希比较
        if (pattern->pattern_hash != 0 && pattern->pattern_hash != line_hash) {
            continue;
        }
        
        // 精确匹配
        if (at_parser_pattern_match(line, pattern)) {
            return pattern;
        }
    }
    
    return NULL;
}

/**
 * @brief 处理匹配的响应 - 这就是您问的 process_response 功能
 */
static OPERATE_RET at_parser_process_response(AT_PARSER_T *parser, const char *line, at_response_pattern_t *pattern)
{
    OPERATE_RET rt = OPRT_OK;
    
    PR_DEBUG("Processing response: %s, type: %d, is_final: %d", 
             line, pattern->response_type, pattern->is_final);
    
    if (pattern->is_final) {
        // 这是最终响应，命令执行完毕
        if (pattern->response_type == AT_RESPONSE_TYPE_FINAL_OK) {
            // 命令成功完成
            PR_INFO("Command completed successfully");
            at_parser_notify_command_success(parser, line);
        } else if (pattern->response_type == AT_RESPONSE_TYPE_FINAL_ERROR) {
            // 命令失败
            PR_ERR("Command failed: %s", line);
            at_parser_notify_command_error(parser, line);
        }
        
        // 可以发送下一个命令了
        at_parser_send_next_command(parser);
        
    } else {
        // 这是中间响应，继续等待更多数据
        switch (pattern->response_type) {
            case AT_RESPONSE_TYPE_ECHO:
                PR_DEBUG("Command echo received: %s", line);
                break;
                
            case AT_RESPONSE_TYPE_INTERMEDIATE:
                PR_DEBUG("Intermediate response: %s", line);
                at_parser_collect_intermediate_response(parser, line);
                break;
                
            case AT_RESPONSE_TYPE_URC:
                PR_INFO("URC received: %s", line);
                at_parser_handle_urc(parser, line, pattern);
                break;
                
            default:
                PR_WARN("Unknown response type: %d", pattern->response_type);
                break;
        }
    }
    
    return rt;
}

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

OPERATE_RET at_parser_line_input(AT_PARSER_HANDLE handle, char *data, uint32_t length)
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

OPERATE_RET at_parser_response_pattern_reg(AT_PARSER_HANDLE handle, AT_RESPONSE_PATTERN_T *pattern, uint32_t pattern_count)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(handle, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(pattern, OPRT_INVALID_PARM);

    AT_PARSER_T *parser = (AT_PARSER_T *)handle;

    if (parser->magic != AT_PARSER_MAGIC) {
        PR_ERR("Invalid AT parser magic number");
        return OPRT_INVALID_PARM;
    }

    parser->pattern = pattern;
    parser->pattern_count = pattern_count;

    // Here you would typically register the pattern in a list or hash table
    // For simplicity, we will just log the pattern registration
    for (uint32_t i = 0; i < pattern_count; i++) {
        PR_DEBUG("Registered response pattern[%d]: %s, ", i, pattern[i].pattern);
    }

    return rt;
}
