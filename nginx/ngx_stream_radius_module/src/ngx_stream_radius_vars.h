/*
 * ngx_stream_radius_vars.h
 *
 * Copyright (C) 2024 - nginx-radius-module contributors
 * License: BSD 2-Clause
 */

#ifndef NGX_STREAM_RADIUS_VARS_H
#define NGX_STREAM_RADIUS_VARS_H

#include "ngx_stream_radius_module.h"

ngx_int_t ngx_stream_radius_var_by_code(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data);

ngx_int_t ngx_stream_radius_var_vsa(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data);

ngx_int_t ngx_stream_radius_var_handler(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data);

#endif /* NGX_STREAM_RADIUS_VARS_H */
