/*
 * ngx_stream_radius_parser.h
 *
 * Copyright (C) 2024 - nginx-radius-module contributors
 * License: BSD 2-Clause
 */

#ifndef NGX_STREAM_RADIUS_PARSER_H
#define NGX_STREAM_RADIUS_PARSER_H

#include "ngx_stream_radius_module.h"

ngx_int_t   ngx_stream_radius_parse_packet(ngx_stream_radius_ctx_t *ctx,
                const u_char *data, size_t len, ngx_log_t *log);

const char *ngx_stream_radius_code_name(ngx_uint_t code);

#endif /* NGX_STREAM_RADIUS_PARSER_H */
