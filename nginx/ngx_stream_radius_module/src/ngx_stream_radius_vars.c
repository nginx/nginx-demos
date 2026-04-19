/*
 * ngx_stream_radius_vars.c
 *
 * Dynamic NGINX variable handlers for RADIUS attributes:
 *   $radius_attr_<N>          — attribute by numeric type code
 *   $radius_vsa_<VendorName>_<AttrName> — vendor-specific attributes
 *   $radius_attr_<AttrName>   — attribute by dictionary name (dynamic)
 *
 * Copyright (C) 2024 - nginx-radius-module contributors
 * License: BSD 2-Clause
 */

#include "ngx_stream_radius_module.h"
#include "ngx_stream_radius_vars.h"
#include "ngx_stream_radius_dict.h"


/* =========================================================================
 * $radius_attr_<N> — lookup by numeric type code
 *    Variable name handler called by NGINX when the variable name begins
 *    with "radius_attr_" followed by digits.
 * ====================================================================== */

ngx_int_t
ngx_stream_radius_var_by_code(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_stream_radius_ctx_t   *ctx;
    ngx_stream_radius_attr_t  *attr;
    ngx_uint_t                 i, type;

    type = (ngx_uint_t) data;

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_radius_module);
    if (ctx == NULL || ctx->parse_error) {
        v->not_found = 1;
        return NGX_OK;
    }

    attr = ctx->attrs;
    for (i = 0; i < ctx->nattrs; i++, attr++) {
        if (attr->type == type && attr->vendor_id == 0) {
            v->data      = attr->value.data;
            v->len       = attr->value.len;
            v->valid     = 1;
            v->not_found = 0;
            v->no_cacheable = 0;
            return NGX_OK;
        }
    }

    v->not_found = 1;
    return NGX_OK;
}


/* =========================================================================
 * $radius_vsa_<VendorName>_<AttrName>
 *
 * The data pointer encodes:  (vendor_id << 8) | vendor_type (see module.h macros)
 * ====================================================================== */

ngx_int_t
ngx_stream_radius_var_vsa(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_stream_radius_ctx_t   *ctx;
    ngx_stream_radius_attr_t  *attr;
    ngx_uint_t                 i;
    uint32_t                   vendor_id;
    uint8_t                    vendor_type;

    /*
     * data encodes:  bits 63..8 = vendor_id (32-bit IANA PEN)
     *                bits  7..0 = vendor sub-attr type
     * See NGX_RADIUS_VSA_DATA / NGX_RADIUS_VSA_VENDOR / NGX_RADIUS_VSA_ATYPE
     * in ngx_stream_radius_module.h
     */
    vendor_id   = NGX_RADIUS_VSA_VENDOR(data);
    vendor_type = NGX_RADIUS_VSA_ATYPE(data);

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_radius_module);
    if (ctx == NULL || ctx->parse_error) {
        v->not_found = 1;
        return NGX_OK;
    }

    attr = ctx->attrs;
    for (i = 0; i < ctx->nattrs; i++, attr++) {
        if (attr->vendor_id == (ngx_uint_t) vendor_id
            && attr->vendor_type == (ngx_uint_t) vendor_type)
        {
            v->data         = attr->value.data;
            v->len          = attr->value.len;
            v->valid        = 1;
            v->not_found    = 0;
            v->no_cacheable = 0;
            return NGX_OK;
        }
    }

    v->not_found = 1;
    return NGX_OK;
}


/* =========================================================================
 * Dynamic variable handler — called by NGINX for unknown variable names
 * that start with our registered prefixes.
 *
 * Resolves:
 *   $radius_attr_<name>   → look up name in dict, find type_code
 *   $radius_vsa_<v>_<n>   → look up vendor+attr name in dict
 * ====================================================================== */

ngx_int_t
ngx_stream_radius_var_handler(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_stream_radius_ctx_t      *ctx;
    ngx_stream_radius_srv_conf_t *rcf;
    ngx_stream_radius_attr_t     *attr;
    ngx_uint_t                    i;
    ngx_str_t                    *varname;
    ngx_str_t                     suffix;
    ngx_radius_attr_def_t        *def;
    ngx_radius_vendor_def_t      *vendor;

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_radius_module);
    if (ctx == NULL || ctx->parse_error) {
        v->not_found = 1;
        return NGX_OK;
    }

    rcf = ctx->rcf;

    /*
     * 'data' carries a pointer to the ngx_str_t variable name.
     * (This is set by ngx_stream_radius_var_get_handler below.)
     */
    varname = (ngx_str_t *) data;

    /* --- Try $radius_attr_<name> --- */
    if (varname->len > rcf->var_prefix.len
        && ngx_strncasecmp(varname->data, rcf->var_prefix.data,
                           rcf->var_prefix.len) == 0)
    {
        suffix.data = varname->data + rcf->var_prefix.len;
        suffix.len  = varname->len  - rcf->var_prefix.len;

        /* Check if the suffix is purely numeric → look up by code */
        ngx_uint_t all_digits = 1;
        for (i = 0; i < suffix.len; i++) {
            if (suffix.data[i] < '0' || suffix.data[i] > '9') {
                all_digits = 0; break;
            }
        }

        if (all_digits && suffix.len > 0) {
            ngx_uint_t code = ngx_atoi(suffix.data, suffix.len);
            attr = ctx->attrs;
            for (i = 0; i < ctx->nattrs; i++, attr++) {
                if (attr->type == code && attr->vendor_id == 0) {
                    v->data = attr->value.data; v->len = attr->value.len;
                    v->valid = 1; v->not_found = 0; v->no_cacheable = 0;
                    return NGX_OK;
                }
            }
        } else {
            /* Name lookup — normalise hyphens/underscores */
            def = ctx->dict ? ctx->dict->attrs.elts : NULL;
            ngx_uint_t ndefs = ctx->dict ? ctx->dict->attrs.nelts : 0;
            for (i = 0; i < ndefs; i++) {
                /* Compare names ignoring hyphen/underscore difference */
                if (def[i].name.len != suffix.len) { continue; }
                ngx_uint_t match = 1;
                for (ngx_uint_t k = 0; k < suffix.len; k++) {
                    u_char a = def[i].name.data[k];
                    u_char b = suffix.data[k];
                    if (a == '-') a = '_';
                    if (b == '-') b = '_';
                    if (ngx_tolower(a) != ngx_tolower(b)) {
                        match = 0; break;
                    }
                }
                if (!match) { continue; }

                /* Found definition — scan attrs for this type */
                attr = ctx->attrs;
                for (ngx_uint_t j = 0; j < ctx->nattrs; j++, attr++) {
                    if (attr->type == def[i].type_code && attr->vendor_id == 0) {
                        v->data = attr->value.data; v->len = attr->value.len;
                        v->valid = 1; v->not_found = 0; v->no_cacheable = 0;
                        return NGX_OK;
                    }
                }
            }
        }
    }

    /* --- Try $radius_vsa_<VendorName>_<AttrName> (VSA dynamic lookup) --- */
    if (varname->len > rcf->vsa_prefix.len
        && ngx_strncasecmp(varname->data, rcf->vsa_prefix.data,
                           rcf->vsa_prefix.len) == 0 && ctx->dict)
    {
        suffix.data = varname->data + rcf->vsa_prefix.len;
        suffix.len  = varname->len  - rcf->vsa_prefix.len;

        /* Find the first underscore — splits vendor name from attr name */
        u_char *sep = (u_char *) ngx_strlchr(suffix.data,
                                              suffix.data + suffix.len, '_');
        if (sep) {
            ngx_str_t vname = { (size_t)(sep - suffix.data), suffix.data };
            ngx_str_t aname = { suffix.len - vname.len - 1, sep + 1 };

            vendor = ctx->dict->vendors.elts;
            for (i = 0; i < ctx->dict->vendors.nelts; i++) {
                if (vendor[i].name.len != vname.len) { continue; }
                if (ngx_strncasecmp(vendor[i].name.data, vname.data,
                                    vname.len) != 0) { continue; }

                /* Found vendor — search its attrs */
                def = vendor[i].attrs.elts;
                for (ngx_uint_t k = 0; k < vendor[i].attrs.nelts; k++) {
                    if (def[k].name.len != aname.len) { continue; }
                    ngx_uint_t match = 1;
                    for (ngx_uint_t m = 0; m < aname.len; m++) {
                        u_char a = def[k].name.data[m];
                        u_char b = aname.data[m];
                        if (a == '-') a = '_';
                        if (b == '-') b = '_';
                        if (ngx_tolower(a) != ngx_tolower(b)) {
                            match = 0; break;
                        }
                    }
                    if (!match) { continue; }

                    /* Match — find in parsed attributes */
                    attr = ctx->attrs;
                    for (ngx_uint_t j = 0; j < ctx->nattrs; j++, attr++) {
                        if (attr->vendor_id == vendor[i].id
                            && attr->vendor_type == def[k].type_code)
                        {
                            v->data = attr->value.data;
                            v->len  = attr->value.len;
                            v->valid = 1; v->not_found = 0;
                            v->no_cacheable = 0;
                            return NGX_OK;
                        }
                    }
                }
            }
        }
    }

    v->not_found = 1;
    return NGX_OK;
}
