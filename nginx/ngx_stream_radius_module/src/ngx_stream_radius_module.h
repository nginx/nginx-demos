/*
 * ngx_stream_radius_module.h
 *
 * NGINX Stream RADIUS Protocol Parser Module — main header
 *
 * Copyright (C) 2024 - nginx-radius-module contributors
 * License: BSD 2-Clause
 */

#ifndef NGX_STREAM_RADIUS_MODULE_H
#define NGX_STREAM_RADIUS_MODULE_H

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>


/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */

#define NGX_RADIUS_HEADER_LEN            20
#define NGX_RADIUS_MAX_PACKET_LEN        4096
#define NGX_RADIUS_DEFAULT_BUFFER_SIZE   4096
#define NGX_RADIUS_AUTHENTICATOR_LEN     16

#define NGX_RADIUS_MAX_ATTRS             255
#define NGX_RADIUS_MAX_ATTR_VALUE_LEN    253
#define NGX_RADIUS_MAX_VSA_VALUE_LEN     247

#define NGX_RADIUS_DEFAULT_VAR_PREFIX    "radius_"
#define NGX_RADIUS_DEFAULT_VSA_PREFIX    "radius_vsa_"

/* RADIUS packet codes (RFC 2865, RFC 2866) */
#define NGX_RADIUS_CODE_ACCESS_REQUEST        1
#define NGX_RADIUS_CODE_ACCESS_ACCEPT         2
#define NGX_RADIUS_CODE_ACCESS_REJECT         3
#define NGX_RADIUS_CODE_ACCOUNTING_REQUEST    4
#define NGX_RADIUS_CODE_ACCOUNTING_RESPONSE   5
#define NGX_RADIUS_CODE_ACCESS_CHALLENGE     11
#define NGX_RADIUS_CODE_STATUS_SERVER        12
#define NGX_RADIUS_CODE_STATUS_CLIENT        13
#define NGX_RADIUS_CODE_DISCONNECT_REQUEST   40
#define NGX_RADIUS_CODE_DISCONNECT_ACK       41
#define NGX_RADIUS_CODE_DISCONNECT_NAK       42
#define NGX_RADIUS_CODE_COA_REQUEST          43
#define NGX_RADIUS_CODE_COA_ACK              44
#define NGX_RADIUS_CODE_COA_NAK              45

/* Standard attribute type 26 = Vendor-Specific */
#define NGX_RADIUS_ATTR_VENDOR_SPECIFIC      26

/* Attribute data types */
#define NGX_RADIUS_TYPE_STRING               1
#define NGX_RADIUS_TYPE_INTEGER              2
#define NGX_RADIUS_TYPE_IPADDR               3
#define NGX_RADIUS_TYPE_DATE                 4
#define NGX_RADIUS_TYPE_OCTETS               5
#define NGX_RADIUS_TYPE_IPV6ADDR             6
#define NGX_RADIUS_TYPE_IPV6PREFIX           7
#define NGX_RADIUS_TYPE_IFID                 8
#define NGX_RADIUS_TYPE_INTEGER64            9
#define NGX_RADIUS_TYPE_ETHER               10

/* Variable selector constants (packet-level variables) */
#define NGX_RADIUS_VAR_CODE          100
#define NGX_RADIUS_VAR_CODE_NAME     101
#define NGX_RADIUS_VAR_IDENTIFIER    102
#define NGX_RADIUS_VAR_LENGTH        103
#define NGX_RADIUS_VAR_AUTHENTICATOR 104
#define NGX_RADIUS_VAR_VALID         105
#define NGX_RADIUS_VAR_RESP_CODE     106
#define NGX_RADIUS_VAR_RESP_NAME     107
#define NGX_RADIUS_VAR_RESP_ATTR     108  /* data = attr type code */


/* -------------------------------------------------------------------------
 * Dictionary structures
 * ---------------------------------------------------------------------- */

typedef struct {
    ngx_str_t    name;          /* attribute name  */
    ngx_uint_t   type_code;     /* attribute type number */
    ngx_uint_t   data_type;     /* NGX_RADIUS_TYPE_* */
    ngx_uint_t   vendor_id;     /* 0 = standard */
} ngx_radius_attr_def_t;

typedef struct {
    ngx_uint_t   id;            /* IANA Private Enterprise Number */
    ngx_str_t    name;
    ngx_array_t  attrs;         /* array of ngx_radius_attr_def_t */
} ngx_radius_vendor_def_t;

typedef struct {
    ngx_pool_t   *pool;
    ngx_array_t   attrs;        /* standard attrs: ngx_radius_attr_def_t */
    ngx_array_t   vendors;      /* ngx_radius_vendor_def_t */
} ngx_radius_dict_t;


/* -------------------------------------------------------------------------
 * Parsed packet structures
 * ---------------------------------------------------------------------- */

typedef struct {
    ngx_uint_t   type;          /* attribute type code (1-255) */
    ngx_uint_t   vendor_id;     /* 0 = standard, >0 = VSA */
    ngx_uint_t   vendor_type;   /* vendor attribute sub-type */
    ngx_str_t    value;         /* decoded, human-readable value string */
    ngx_str_t    raw;           /* raw bytes */
    ngx_uint_t   data_type;     /* NGX_RADIUS_TYPE_* */
} ngx_stream_radius_attr_t;


/* -------------------------------------------------------------------------
 * Main configuration  (one per nginx instance)
 *
 * The dictionary is built here so all server blocks share it, and so
 * VSA variable registration — which must happen at configuration parse
 * time — can use it as soon as a radius_dict directive is processed.
 * ---------------------------------------------------------------------- */

typedef struct {
    ngx_radius_dict_t   *dict;       /* NULL until first radius_dict call */
} ngx_stream_radius_main_conf_t;


/* -------------------------------------------------------------------------
 * Server configuration
 * ---------------------------------------------------------------------- */

typedef struct {
    ngx_flag_t           enabled;
    size_t               buffer_size;
    ngx_str_t            secret;
    ngx_str_t            var_prefix;
    ngx_str_t            vsa_prefix;
    /* dict pointer copied from main_conf at postconfiguration */
    ngx_radius_dict_t   *dict;
} ngx_stream_radius_srv_conf_t;


/* -------------------------------------------------------------------------
 * Per-session context
 * ---------------------------------------------------------------------- */

typedef struct {
    ngx_pool_t                   *pool;   /* connection pool */
    ngx_buf_t                    *buf;    /* unused; kept for ABI compat */
    size_t                        pkt_len;

    ngx_uint_t                    code;
    ngx_uint_t                    identifier;
    u_char                        authenticator[NGX_RADIUS_AUTHENTICATOR_LEN];

    ngx_stream_radius_attr_t      attrs[NGX_RADIUS_MAX_ATTRS];
    ngx_uint_t                    nattrs;

    ngx_uint_t                    parse_error;

    /* upstream response fields (populated via send_chain intercept) */
    ngx_uint_t                    response_code;
    ngx_uint_t                    response_parsed;
    size_t                        resp_pkt_len;
    ngx_stream_radius_attr_t      resp_attrs[NGX_RADIUS_MAX_ATTRS];
    ngx_uint_t                    resp_nattrs;
    /* scratch buffer used to gather chain data for response parsing */
    u_char                        resp_buf[NGX_RADIUS_MAX_PACKET_LEN];

    /* saved original send_chain so we can restore/chain it */
    ngx_send_chain_pt             original_send_chain;

    ngx_stream_radius_srv_conf_t *rcf;
    ngx_radius_dict_t            *dict;
} ngx_stream_radius_ctx_t;


/* -------------------------------------------------------------------------
 * VSA variable data encoding
 *
 * The uintptr_t 'data' field of a VSA variable encodes:
 *   bits 63..8  : vendor_id  (32 significant bits, zero-extended)
 *   bits  7..0  : attr_type  (vendor sub-attribute code, 1-255)
 *
 * Macros to pack/unpack:
 * ---------------------------------------------------------------------- */

#define NGX_RADIUS_VSA_DATA(vid, atype) \
    (uintptr_t)(((uintptr_t)(uint32_t)(vid) << 8) | (uint8_t)(atype))

#define NGX_RADIUS_VSA_VENDOR(data)  ((uint32_t)((uintptr_t)(data) >> 8))
#define NGX_RADIUS_VSA_ATYPE(data)   ((uint8_t)((uintptr_t)(data) & 0xff))


/* -------------------------------------------------------------------------
 * Helper: build a VSA nginx variable name into buf
 *   "radius_vsa_<vendor_lower_underscore>_<attr_lower_underscore>"
 * Returns the number of bytes written (excluding NUL), or 0 on overflow.
 * ---------------------------------------------------------------------- */

size_t ngx_stream_radius_vsa_var_name(u_char *buf, size_t buf_len,
    const ngx_str_t *vendor_name, const ngx_str_t *attr_name);


/* -------------------------------------------------------------------------
 * Module declaration
 * ---------------------------------------------------------------------- */

extern ngx_module_t  ngx_stream_radius_module;

#endif /* NGX_STREAM_RADIUS_MODULE_H */
