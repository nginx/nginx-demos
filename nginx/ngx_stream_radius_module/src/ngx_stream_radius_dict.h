/*
 * ngx_stream_radius_dict.h
 *
 * Copyright (C) 2024 - nginx-radius-module contributors
 * License: BSD 2-Clause
 */

#ifndef NGX_STREAM_RADIUS_DICT_H
#define NGX_STREAM_RADIUS_DICT_H

#include "ngx_stream_radius_module.h"

ngx_radius_dict_t     *ngx_stream_radius_dict_create(ngx_pool_t *pool);
ngx_int_t              ngx_stream_radius_dict_load_builtins(
                            ngx_radius_dict_t *dict);
ngx_int_t              ngx_stream_radius_dict_load_file(
                            ngx_radius_dict_t *dict, ngx_str_t *path,
                            ngx_log_t *log);
ngx_radius_attr_def_t *ngx_stream_radius_dict_lookup(
                            ngx_radius_dict_t *dict, ngx_uint_t type_code);
ngx_radius_attr_def_t *ngx_stream_radius_dict_lookup_vsa(
                            ngx_radius_dict_t *dict, ngx_uint_t vendor_id,
                            ngx_uint_t vendor_type);

#endif /* NGX_STREAM_RADIUS_DICT_H */
