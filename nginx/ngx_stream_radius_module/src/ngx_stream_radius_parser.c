/*
 * ngx_stream_radius_parser.c
 *
 * RADIUS packet parser — RFC 2865 / RFC 2866
 *
 * Copyright (C) 2024 - nginx-radius-module contributors
 * License: BSD 2-Clause
 */

#include "ngx_stream_radius_module.h"
#include "ngx_stream_radius_parser.h"
#include "ngx_stream_radius_dict.h"

#include <arpa/inet.h>


/* -------------------------------------------------------------------------
 * Helper: convert 4-byte big-endian to uint32
 * ---------------------------------------------------------------------- */

static ngx_inline uint32_t
ngx_radius_get_uint32(const u_char *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
            (uint32_t)p[3];
}


/* -------------------------------------------------------------------------
 * Map a RADIUS packet code to a human-readable string
 * ---------------------------------------------------------------------- */

const char *
ngx_stream_radius_code_name(ngx_uint_t code)
{
    switch (code) {
    case NGX_RADIUS_CODE_ACCESS_REQUEST:      return "Access-Request";
    case NGX_RADIUS_CODE_ACCESS_ACCEPT:       return "Access-Accept";
    case NGX_RADIUS_CODE_ACCESS_REJECT:       return "Access-Reject";
    case NGX_RADIUS_CODE_ACCOUNTING_REQUEST:  return "Accounting-Request";
    case NGX_RADIUS_CODE_ACCOUNTING_RESPONSE: return "Accounting-Response";
    case NGX_RADIUS_CODE_ACCESS_CHALLENGE:    return "Access-Challenge";
    case NGX_RADIUS_CODE_STATUS_SERVER:       return "Status-Server";
    case NGX_RADIUS_CODE_STATUS_CLIENT:       return "Status-Client";
    case NGX_RADIUS_CODE_DISCONNECT_REQUEST:  return "Disconnect-Request";
    case NGX_RADIUS_CODE_DISCONNECT_ACK:      return "Disconnect-ACK";
    case NGX_RADIUS_CODE_DISCONNECT_NAK:      return "Disconnect-NAK";
    case NGX_RADIUS_CODE_COA_REQUEST:         return "CoA-Request";
    case NGX_RADIUS_CODE_COA_ACK:             return "CoA-ACK";
    case NGX_RADIUS_CODE_COA_NAK:             return "CoA-NAK";
    default:                                  return "Unknown";
    }
}


/* -------------------------------------------------------------------------
 * Decode a standard or vendor attribute value to a printable string.
 * Returns NGX_OK on success; caller must have pre-allocated attr->value.data.
 * ---------------------------------------------------------------------- */

static ngx_int_t
ngx_radius_decode_value(ngx_pool_t *pool, ngx_stream_radius_attr_t *attr,
    const u_char *data, size_t len, ngx_uint_t dtype, ngx_log_t *log)
{
    struct in_addr   addr4;
    struct in6_addr  addr6;
    char             ip_str[INET6_ADDRSTRLEN];
    u_char          *p;
    uint32_t         ival;

    /* Store raw bytes */
    attr->raw.data = ngx_pnalloc(pool, len);
    if (attr->raw.data == NULL) { return NGX_ERROR; }
    ngx_memcpy(attr->raw.data, data, len);
    attr->raw.len = len;

    switch (dtype) {

    case NGX_RADIUS_TYPE_INTEGER:
        if (len != 4) {
            ngx_log_debug1(NGX_LOG_DEBUG_STREAM, log, 0,
                           "radius: integer attr has wrong length %uz", len);
            goto fallback_hex;
        }
        ival = ngx_radius_get_uint32(data);
        p = ngx_pnalloc(pool, NGX_INT32_LEN + 1);
        if (p == NULL) { return NGX_ERROR; }
        attr->value.len  = ngx_sprintf(p, "%uD", ival) - p;
        attr->value.data = p;
        break;

    case NGX_RADIUS_TYPE_IPADDR:
        if (len != 4) { goto fallback_hex; }
        ngx_memcpy(&addr4.s_addr, data, 4);
        inet_ntop(AF_INET, &addr4, ip_str, sizeof(ip_str));
        attr->value.data = (u_char *) ngx_pstrdup(pool,
            &(ngx_str_t){ ngx_strlen(ip_str), (u_char *) ip_str });
        attr->value.len  = ngx_strlen(ip_str);
        break;

    case NGX_RADIUS_TYPE_IPV6ADDR:
        if (len != 16) { goto fallback_hex; }
        ngx_memcpy(&addr6, data, 16);
        inet_ntop(AF_INET6, &addr6, ip_str, sizeof(ip_str));
        attr->value.data = (u_char *) ngx_pstrdup(pool,
            &(ngx_str_t){ ngx_strlen(ip_str), (u_char *) ip_str });
        attr->value.len  = ngx_strlen(ip_str);
        break;

    case NGX_RADIUS_TYPE_STRING:
        p = ngx_pnalloc(pool, len + 1);
        if (p == NULL) { return NGX_ERROR; }
        ngx_memcpy(p, data, len);
        p[len] = '\0';
        attr->value.data = p;
        attr->value.len  = len;
        break;

    case NGX_RADIUS_TYPE_DATE:
        /* 32-bit UNIX timestamp */
        if (len != 4) { goto fallback_hex; }
        ival = ngx_radius_get_uint32(data);
        p = ngx_pnalloc(pool, NGX_INT32_LEN + 1);
        if (p == NULL) { return NGX_ERROR; }
        attr->value.len  = ngx_sprintf(p, "%uD", ival) - p;
        attr->value.data = p;
        break;

    case NGX_RADIUS_TYPE_OCTETS:
    default:
fallback_hex:
        /* Hex-encode raw bytes */
        p = ngx_pnalloc(pool, len * 2 + 3);
        if (p == NULL) { return NGX_ERROR; }
        p[0] = '0'; p[1] = 'x';
        ngx_hex_dump(p + 2, (u_char *) data, len);
        attr->value.data = p;
        attr->value.len  = len * 2 + 2;
        break;
    }

    return NGX_OK;
}


/* -------------------------------------------------------------------------
 * Parse a Vendor-Specific Attribute (type 26) per RFC 2865 §5.26
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |     Type=26   |    Length     |            Vendor-Id          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Vendor-Id (cont)             |  Vendor-Type  | Vendor-Length |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |    Value ...
 * +-+-+-+-+-+-+-+-+-+-
 * ---------------------------------------------------------------------- */

static ngx_int_t
ngx_radius_parse_vsa(ngx_stream_radius_ctx_t *ctx, const u_char *data,
    size_t len, ngx_log_t *log)
{
    uint32_t                  vendor_id;
    ngx_uint_t                vtype;
    size_t                    vlen;
    const u_char             *p, *end;
    ngx_stream_radius_attr_t *attr;
    ngx_radius_attr_def_t    *def;
    ngx_uint_t                dtype;
    ngx_pool_t               *pool = ctx->pool;

    if (len < 6) {
        ngx_log_debug0(NGX_LOG_DEBUG_STREAM, log, 0,
                       "radius: VSA too short");
        return NGX_DECLINED;
    }

    vendor_id = ngx_radius_get_uint32(data);
    p   = data + 4;
    end = data + len;

    while (p + 2 <= end) {
        vtype = p[0];
        vlen  = p[1];

        if (vlen < 2 || p + vlen > end) {
            ngx_log_debug0(NGX_LOG_DEBUG_STREAM, log, 0,
                           "radius: malformed VSA sub-attribute");
            break;
        }

        if (ctx->nattrs >= NGX_RADIUS_MAX_ATTRS) {
            ngx_log_error(NGX_LOG_WARN, log, 0,
                          "radius: max attribute count reached, skipping VSAs");
            break;
        }

        attr = &ctx->attrs[ctx->nattrs++];
        ngx_memzero(attr, sizeof(*attr));

        attr->type       = NGX_RADIUS_ATTR_VENDOR_SPECIFIC;
        attr->vendor_id  = vendor_id;
        attr->vendor_type= vtype;

        /* Look up the attribute definition in the dictionary */
        def = ngx_stream_radius_dict_lookup_vsa(ctx->dict, vendor_id, vtype);
        dtype = (def != NULL) ? def->data_type : NGX_RADIUS_TYPE_OCTETS;
        attr->data_type = dtype;

        if (ngx_radius_decode_value(pool, attr, p + 2, vlen - 2,
                                    dtype, log) != NGX_OK)
        {
            return NGX_ERROR;
        }

        p += vlen;
    }

    return NGX_OK;
}


/* =========================================================================
 * Main packet parser
 * ====================================================================== */

ngx_int_t
ngx_stream_radius_parse_packet(ngx_stream_radius_ctx_t *ctx,
    const u_char *data, size_t len, ngx_log_t *log)
{
    const u_char             *p, *end;
    ngx_uint_t                atype;
    size_t                    alen;
    ngx_stream_radius_attr_t *attr;
    ngx_radius_attr_def_t    *def;
    ngx_uint_t                dtype;
    ngx_pool_t               *pool;

    if (len < NGX_RADIUS_HEADER_LEN) {
        ngx_log_error(NGX_LOG_WARN, log, 0,
                      "radius: packet too short (%uz bytes)", len);
        return NGX_ERROR;
    }

    pool = ctx->pool;

    /* --- Parse fixed header --- */
    ctx->code       = data[0];
    ctx->identifier = data[1];
    /* length at bytes 2-3 already validated by caller */
    ngx_memcpy(ctx->authenticator, data + 4, NGX_RADIUS_AUTHENTICATOR_LEN);

    ngx_log_debug3(NGX_LOG_DEBUG_STREAM, log, 0,
                   "radius: code=%d id=%d len=%uz",
                   ctx->code, ctx->identifier, len);

    /* --- Walk AVP list --- */
    p   = data + NGX_RADIUS_HEADER_LEN;
    end = data + len;

    while (p < end) {
        if (p + 2 > end) {
            ngx_log_error(NGX_LOG_WARN, log, 0,
                          "radius: truncated AVP at offset %uz",
                          (size_t)(p - data));
            return NGX_ERROR;
        }

        atype = p[0];
        alen  = p[1];

        if (alen < 2) {
            ngx_log_error(NGX_LOG_WARN, log, 0,
                          "radius: zero-length AVP (type=%d)", atype);
            return NGX_ERROR;
        }

        if (p + alen > end) {
            ngx_log_error(NGX_LOG_WARN, log, 0,
                          "radius: AVP length exceeds packet (type=%d)", atype);
            return NGX_ERROR;
        }

        /* Vendor-Specific Attribute (type 26) gets special treatment */
        if (atype == NGX_RADIUS_ATTR_VENDOR_SPECIFIC) {
            if (ngx_radius_parse_vsa(ctx, p + 2, alen - 2, log) == NGX_ERROR) {
                return NGX_ERROR;
            }
            p += alen;
            continue;
        }

        if (ctx->nattrs >= NGX_RADIUS_MAX_ATTRS) {
            ngx_log_error(NGX_LOG_WARN, log, 0,
                          "radius: max attribute count reached");
            break;
        }

        attr = &ctx->attrs[ctx->nattrs++];
        ngx_memzero(attr, sizeof(*attr));

        attr->type      = atype;
        attr->vendor_id = 0;

        /* Look up data type from dictionary */
        def = ngx_stream_radius_dict_lookup(ctx->dict, atype);
        dtype = (def != NULL) ? def->data_type : NGX_RADIUS_TYPE_OCTETS;
        attr->data_type = dtype;

        if (ngx_radius_decode_value(pool, attr, p + 2, alen - 2,
                                    dtype, log) != NGX_OK)
        {
            return NGX_ERROR;
        }

        ngx_log_debug3(NGX_LOG_DEBUG_STREAM, log, 0,
                       "radius: attr type=%d len=%uz value=\"%V\"",
                       atype, alen - 2, &attr->value);

        p += alen;
    }

    return NGX_OK;
}
