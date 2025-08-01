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
typedef struct {
    uint32_t magic; // Magic number for validation

    // line ending
    char line_ending[LINE_ENDING_MAX_LENGTH];

    AT_LINE_T *line_head;         // Pointer to the head of the line list
    AT_LINE_T *line_tail;         // Pointer to the tail of the line list
    volatile uint32_t line_count; // Count of lines processed

    // response pattern
    AT_RESPONSE_PATTERN_T *pattern_head;
    AT_RESPONSE_PATTERN_T *pattern_tail; // Pointer to the tail of the pattern list
    volatile uint32_t pattern_count;     // Count of response patterns registered
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

OPERATE_RET at_parser_deinit(AT_PARSER_HANDLE handle)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(handle, OPRT_INVALID_PARM);

    AT_PARSER_T *parser = (AT_PARSER_T *)handle;

    if (parser->magic != AT_PARSER_MAGIC) {
        PR_ERR("Invalid AT parser magic number");
        return OPRT_INVALID_PARM;
    }

    // Free all lines
    AT_LINE_T *current_line = parser->line_head;
    while (current_line) {
        AT_LINE_T *next_line = current_line->next;
        at_parser_free_line(current_line);
        current_line = next_line;
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

    parser->line_count++;

    PR_DEBUG("Added line: %s", new_line->data);
    // PR_HEXDUMP_DEBUG("Added line", new_line->data, new_line->length);

    return OPRT_OK;

__ERR:
    at_parser_free_line(new_line);

    return rt;
}

OPERATE_RET at_parser_remove_line(AT_PARSER_HANDLE handle, AT_LINE_T *line)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(handle, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(line, OPRT_INVALID_PARM);

    AT_PARSER_T *parser = (AT_PARSER_T *)handle;

    if (parser->magic != AT_PARSER_MAGIC) {
        PR_ERR("Invalid AT parser magic number");
        return OPRT_INVALID_PARM;
    }

    if (parser->line_head == NULL) {
        PR_ERR("No lines to remove");
        return OPRT_INVALID_PARM;
    }

    // Find the line in the list
    AT_LINE_T *current = parser->line_head;
    AT_LINE_T *previous = NULL;

    while (current) {
        if (current == line) {
            if (previous) {
                previous->next = current->next;
            } else {
                parser->line_head = current->next;
            }
            if (parser->line_tail == current) {
                parser->line_tail = previous;
            }
            parser->line_count--;
            at_parser_free_line(current);
            return OPRT_OK;
        }
        previous = current;
        current = current->next;
    }

    PR_ERR("Line not found in parser");
    return OPRT_NOT_FOUND;
}

OPERATE_RET at_parser_split_lines(AT_PARSER_HANDLE handle, AT_LINE_T *split_start, uint32_t count)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(handle, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(split_start, OPRT_INVALID_PARM);

    AT_PARSER_T *parser = (AT_PARSER_T *)handle;

    if (parser->line_head == NULL) {
        PR_ERR("No lines to split");
        return OPRT_INVALID_PARM;
    }

    if (parser->magic != AT_PARSER_MAGIC) {
        PR_ERR("Invalid AT parser magic number");
        return OPRT_INVALID_PARM;
    }

    AT_LINE_T *current = parser->line_head;
    AT_LINE_T *previous = NULL;
    if (split_start == parser->line_head && count >= parser->line_count) {
        parser->line_head = NULL;
        parser->line_tail = NULL;
        parser->line_count = 0;
        PR_DEBUG("All lines removed from parser");
        return OPRT_OK;
    }

    // find the split start line
    previous = NULL;
    current = parser->line_head;
    while (current) {
        if (current == split_start) {
            break;
        }
        previous = current;
        current = current->next;
    }

    if (current != split_start) {
        PR_ERR("Split start line not found in parser");
        return OPRT_NOT_FOUND;
    }

    // find the split end line
    uint32_t split_count = 1;
    AT_LINE_T *split_end = split_start;
    for (uint32_t i = 1; i < count; i++) {
        if (split_end->next != NULL) {
            split_end = split_end->next;
            split_count++;
        } else {
            PR_WARN("Not enough lines to split");
            break;
        }
    }

    if (previous == NULL) {
        parser->line_head = split_end->next;
    } else {
        previous->next = split_end->next;
    }

    if (split_end == parser->line_tail) {
        parser->line_tail = previous; // Update tail if we split the last line
    }
    split_end->next = NULL; // Disconnect the split end line from the list

    // update the line count
    parser->line_count -= split_count;

    return OPRT_OK;
}

OPERATE_RET at_parser_free_line(AT_LINE_T *line)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(line, OPRT_INVALID_PARM);
    PR_DEBUG("Freeing line: %p->%p", line, line->data);

    if (line->data) {
        PR_DEBUG("Freeing line data: %.*s", line->length, line->data);
        tal_free(line->data);
        line->data = NULL;
    }
    PR_DEBUG("Freeing line structure");
    tal_free(line);

    return OPRT_OK;
}

char *at_parser_line_input(AT_PARSER_HANDLE handle, char *data, uint32_t length)
{
    TUYA_CHECK_NULL_RETURN(handle, NULL);
    TUYA_CHECK_NULL_RETURN(data, NULL);

    AT_PARSER_T *parser = (AT_PARSER_T *)handle;

    if (parser->magic != AT_PARSER_MAGIC) {
        PR_ERR("Invalid AT parser magic number");
        return NULL;
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

    return p_start; // Return the next position after the last processed line
}

AT_RESPONSE_PATTERN_T *at_parser_pattern_match(AT_PARSER_HANDLE handle, AT_LINE_T *line)
{
    AT_RESPONSE_PATTERN_T *matched_pattern = NULL;

    TUYA_CHECK_NULL_RETURN(handle, NULL);
    TUYA_CHECK_NULL_RETURN(line, NULL);

    AT_PARSER_T *parser = (AT_PARSER_T *)handle;

    AT_RESPONSE_PATTERN_T *pattern = parser->pattern_head;
    for (uint32_t i = 0; i < parser->pattern_count; i++) {
        if (pattern->match_type == MATCH_EXACT) {
            if (strncmp(line->data, pattern->pattern, line->length) == 0) {
                matched_pattern = pattern;
                break;
            }
        } else if (pattern->match_type == MATCH_PREFIX) {
            if (strncmp(line->data, pattern->pattern, strlen(pattern->pattern)) == 0) {
                matched_pattern = pattern;
                break;
            }
        } else if (pattern->match_type == MATCH_SUFFIX) {
            size_t suffix_length = strlen(pattern->pattern);
            if (line->length >= suffix_length &&
                strncmp(line->data + line->length - suffix_length, pattern->pattern, suffix_length) == 0) {
                matched_pattern = pattern;
                break;
            }
        } else if (pattern->match_type == MATCH_CONTAINS) {
            if (strstr(line->data, pattern->pattern)) {
                matched_pattern = pattern;
                break;
            }
        } else {
            PR_ERR("Unknown match type: %d", pattern->match_type);
            return NULL; // Unknown match type
        }
        pattern = pattern->next;
    }

    return matched_pattern;
}

AT_LINE_T *at_parser_get_line(AT_PARSER_HANDLE handle, uint32_t index)
{
    TUYA_CHECK_NULL_RETURN(handle, NULL);

    AT_PARSER_T *parser = (AT_PARSER_T *)handle;

    if (parser->magic != AT_PARSER_MAGIC) {
        PR_ERR("Invalid AT parser magic number");
        return NULL;
    }

    if (index >= parser->line_count) {
        PR_ERR("Index out of bounds: %d >= %d", index, parser->line_count);
        return NULL;
    }

    AT_LINE_T *current_line = parser->line_head;
    for (uint32_t i = 0; i < index && current_line; i++) {
        current_line = current_line->next;
    }

    return current_line;
}

uint32_t at_parser_get_line_num(AT_PARSER_HANDLE handle)
{
    TUYA_CHECK_NULL_RETURN(handle, 0);

    AT_PARSER_T *parser = (AT_PARSER_T *)handle;

    if (parser->magic != AT_PARSER_MAGIC) {
        PR_ERR("Invalid AT parser magic number");
        return 0;
    }

    return parser->line_count;
}

AT_RESPONSE_PATTERN_T *at_parser_pattern_find(AT_PARSER_HANDLE handle, const char *pattern)
{
    TUYA_CHECK_NULL_RETURN(handle, NULL);
    TUYA_CHECK_NULL_RETURN(pattern, NULL);

    AT_PARSER_T *parser = (AT_PARSER_T *)handle;

    AT_RESPONSE_PATTERN_T *current_pattern = parser->pattern_head;
    while (current_pattern) {
        if (strcmp(current_pattern->pattern, pattern) == 0) {
            return current_pattern;
        }
        current_pattern = current_pattern->next;
    }

    return NULL; // Pattern not found
}

OPERATE_RET at_parser_response_pattern_regist(AT_PARSER_HANDLE handle, AT_RESPONSE_PATTERN_T *pattern)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(handle, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(pattern, OPRT_INVALID_PARM);

    AT_PARSER_T *parser = (AT_PARSER_T *)handle;

    if (parser->magic != AT_PARSER_MAGIC) {
        PR_ERR("Invalid AT parser magic number");
        return OPRT_INVALID_PARM;
    }

    if (at_parser_pattern_find(handle, pattern->pattern) != NULL) {
        PR_WARN("Pattern already registered: %s", pattern->pattern);
        return OPRT_OK;
    }

    if (parser->pattern_head == NULL && parser->pattern_tail == NULL) {
        parser->pattern_head = pattern;
        parser->pattern_tail = pattern;
    } else {
        parser->pattern_tail->next = pattern;
        parser->pattern_tail = pattern;
    }
    pattern->next = NULL; // Ensure the new pattern's next pointer is NULL
    parser->pattern_count++;

    return rt;
}

OPERATE_RET at_parser_response_pattern_unregist(AT_PARSER_HANDLE handle, const char *pattern)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(handle, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(pattern, OPRT_INVALID_PARM);

    AT_PARSER_T *parser = (AT_PARSER_T *)handle;

    if (parser->magic != AT_PARSER_MAGIC) {
        PR_ERR("Invalid AT parser magic number");
        return OPRT_INVALID_PARM;
    }

    AT_RESPONSE_PATTERN_T *current = parser->pattern_head;
    AT_RESPONSE_PATTERN_T *previous = NULL;

    while (current) {
        if (strcmp(current->pattern, pattern) == 0) {
            if (previous) {
                previous->next = current->next;
            } else {
                parser->pattern_head = current->next;
            }
            if (parser->pattern_tail == current) {
                parser->pattern_tail = previous; // Update tail if we removed the last pattern
            }
            parser->pattern_count--;
            return OPRT_OK;
        }
        previous = current;
        current = current->next;
    }

    PR_ERR("Pattern not found: %s", pattern);
    return OPRT_NOT_FOUND;
}
