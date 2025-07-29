/**
 * @file at_parser.h
 * @brief at_parser module is used to
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __AT_PARSER_H__
#define __AT_PARSER_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/
#define LINE_ENDING_MAX_LENGTH 3

#define LINE_ENDING_CRLF "\r\n"
#define LINE_ENDING_LF   "\n"
#define LINE_ENDING_CR   "\r"

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef void *AT_PARSER_HANDLE;

typedef void (*at_parser_callback_t)(const char *line, uint32_t length);

typedef enum {
    AT_RESPONSE_TYPE_ECHO = 0,     /* Command echo */
    AT_RESPONSE_TYPE_INTERMEDIATE, /* Intermediate data response */
    AT_RESPONSE_TYPE_FINAL_OK,     /* Final success response */
    AT_RESPONSE_TYPE_FINAL_ERROR,  /* Final error response */
    AT_RESPONSE_TYPE_URC,          /* Unsolicited Result Code */
    AT_RESPONSE_TYPE_UNKNOWN       /* Unknown response */
} AT_RESPONSE_TYPE_E;

/* Response match types */
typedef enum {
    AT_RESPONSE_MATCH_EXACT = 0, /* Exact string match */
    AT_RESPONSE_MATCH_PREFIX,    /* Prefix match */
    AT_RESPONSE_MATCH_SUFFIX,    /* Suffix match */
    AT_RESPONSE_MATCH_CONTAINS,  /* Contains substring */
    AT_RESPONSE_MATCH_REGEX      /* Regular expression */
} AT_RESPONSE_MATCH_TYPE_E;

typedef struct {
    const char *pattern;                 /* Pattern string */
    AT_RESPONSE_MATCH_TYPE_E match_type; /* Match type (exact, prefix, etc.) */
    AT_RESPONSE_TYPE_E response_type;    /* Response type */
    uint8_t is_final;                    /* Is final response */
    uint32_t pattern_hash;               /* Pre-computed hash for fast lookup */
    at_parser_callback_t callback;       /* Callback for handling response */
} at_response_pattern_t;

typedef struct {
    // This can be used to define how the AT commands are terminated
    char line_ending[LINE_ENDING_MAX_LENGTH]; // Max length of 2 for "\r\n"
} AT_PARSER_CFG_T;

/***********************************************************
********************function declaration********************
***********************************************************/

OPERATE_RET at_parser_init(AT_PARSER_HANDLE *handle, AT_PARSER_CFG_T *cfg);

OPERATE_RET at_parser_input(AT_PARSER_HANDLE handle, char *data, uint32_t length);

OPERATE_RET at_parser_deinit(AT_PARSER_HANDLE handle);

OPERATE_RET at_parser_response_pattern_reg(AT_PARSER_HANDLE handle, at_response_pattern_t *pattern);

#ifdef __cplusplus
}
#endif

#endif /* __AT_PARSER_H__ */
