/*
 * ngx_stream_radius_dict.c
 *
 * RADIUS attribute dictionary:
 *   - Built-in RFC 2865 / RFC 2866 attributes
 *   - FreeRADIUS-compatible dictionary file parser
 *   - Vendor-specific attribute support (VSA)
 *
 * Copyright (C) 2024 - nginx-radius-module contributors
 * License: BSD 2-Clause
 */

#include "ngx_stream_radius_module.h"
#include "ngx_stream_radius_dict.h"

#include <sys/stat.h>
#include <fcntl.h>


/* -------------------------------------------------------------------------
 * Built-in RFC 2865 / RFC 2866 attribute table
 * ---------------------------------------------------------------------- */

typedef struct {
    ngx_uint_t   type_code;
    const char  *name;
    ngx_uint_t   data_type;
} ngx_radius_builtin_attr_t;

static ngx_radius_builtin_attr_t ngx_radius_builtin_attrs[] = {
    {  1, "User-Name",              NGX_RADIUS_TYPE_STRING  },
    {  2, "User-Password",          NGX_RADIUS_TYPE_OCTETS  },
    {  3, "CHAP-Password",          NGX_RADIUS_TYPE_OCTETS  },
    {  4, "NAS-IP-Address",         NGX_RADIUS_TYPE_IPADDR  },
    {  5, "NAS-Port",               NGX_RADIUS_TYPE_INTEGER },
    {  6, "Service-Type",           NGX_RADIUS_TYPE_INTEGER },
    {  7, "Framed-Protocol",        NGX_RADIUS_TYPE_INTEGER },
    {  8, "Framed-IP-Address",      NGX_RADIUS_TYPE_IPADDR  },
    {  9, "Framed-IP-Netmask",      NGX_RADIUS_TYPE_IPADDR  },
    { 10, "Framed-Routing",         NGX_RADIUS_TYPE_INTEGER },
    { 11, "Filter-Id",              NGX_RADIUS_TYPE_STRING  },
    { 12, "Framed-MTU",             NGX_RADIUS_TYPE_INTEGER },
    { 13, "Framed-Compression",     NGX_RADIUS_TYPE_INTEGER },
    { 14, "Login-IP-Host",          NGX_RADIUS_TYPE_IPADDR  },
    { 15, "Login-Service",          NGX_RADIUS_TYPE_INTEGER },
    { 16, "Login-TCP-Port",         NGX_RADIUS_TYPE_INTEGER },
    { 18, "Reply-Message",          NGX_RADIUS_TYPE_STRING  },
    { 19, "Callback-Number",        NGX_RADIUS_TYPE_STRING  },
    { 20, "Callback-Id",            NGX_RADIUS_TYPE_STRING  },
    { 22, "Framed-Route",           NGX_RADIUS_TYPE_STRING  },
    { 23, "Framed-IPX-Network",     NGX_RADIUS_TYPE_IPADDR  },
    { 24, "State",                  NGX_RADIUS_TYPE_OCTETS  },
    { 25, "Class",                  NGX_RADIUS_TYPE_OCTETS  },
    { 26, "Vendor-Specific",        NGX_RADIUS_TYPE_OCTETS  },
    { 27, "Session-Timeout",        NGX_RADIUS_TYPE_INTEGER },
    { 28, "Idle-Timeout",           NGX_RADIUS_TYPE_INTEGER },
    { 29, "Termination-Action",     NGX_RADIUS_TYPE_INTEGER },
    { 30, "Called-Station-Id",      NGX_RADIUS_TYPE_STRING  },
    { 31, "Calling-Station-Id",     NGX_RADIUS_TYPE_STRING  },
    { 32, "NAS-Identifier",         NGX_RADIUS_TYPE_STRING  },
    { 33, "Proxy-State",            NGX_RADIUS_TYPE_OCTETS  },
    { 34, "Login-LAT-Service",      NGX_RADIUS_TYPE_STRING  },
    { 35, "Login-LAT-Node",         NGX_RADIUS_TYPE_STRING  },
    { 36, "Login-LAT-Group",        NGX_RADIUS_TYPE_OCTETS  },
    { 37, "Framed-AppleTalk-Link",  NGX_RADIUS_TYPE_INTEGER },
    { 38, "Framed-AppleTalk-Network", NGX_RADIUS_TYPE_INTEGER },
    { 39, "Framed-AppleTalk-Zone",  NGX_RADIUS_TYPE_STRING  },
    /* RFC 2866 Accounting */
    { 40, "Acct-Status-Type",       NGX_RADIUS_TYPE_INTEGER },
    { 41, "Acct-Delay-Time",        NGX_RADIUS_TYPE_INTEGER },
    { 42, "Acct-Input-Octets",      NGX_RADIUS_TYPE_INTEGER },
    { 43, "Acct-Output-Octets",     NGX_RADIUS_TYPE_INTEGER },
    { 44, "Acct-Session-Id",        NGX_RADIUS_TYPE_STRING  },
    { 45, "Acct-Authentic",         NGX_RADIUS_TYPE_INTEGER },
    { 46, "Acct-Session-Time",      NGX_RADIUS_TYPE_INTEGER },
    { 47, "Acct-Input-Packets",     NGX_RADIUS_TYPE_INTEGER },
    { 48, "Acct-Output-Packets",    NGX_RADIUS_TYPE_INTEGER },
    { 49, "Acct-Terminate-Cause",   NGX_RADIUS_TYPE_INTEGER },
    { 50, "Acct-Multi-Session-Id",  NGX_RADIUS_TYPE_STRING  },
    { 51, "Acct-Link-Count",        NGX_RADIUS_TYPE_INTEGER },
    { 52, "Acct-Input-Gigawords",   NGX_RADIUS_TYPE_INTEGER },
    { 53, "Acct-Output-Gigawords",  NGX_RADIUS_TYPE_INTEGER },
    { 55, "Event-Timestamp",        NGX_RADIUS_TYPE_DATE    },
    { 60, "CHAP-Challenge",         NGX_RADIUS_TYPE_OCTETS  },
    { 61, "NAS-Port-Type",          NGX_RADIUS_TYPE_INTEGER },
    { 62, "Port-Limit",             NGX_RADIUS_TYPE_INTEGER },
    { 63, "Login-LAT-Port",         NGX_RADIUS_TYPE_STRING  },
    { 64, "Tunnel-Type",            NGX_RADIUS_TYPE_INTEGER },
    { 65, "Tunnel-Medium-Type",     NGX_RADIUS_TYPE_INTEGER },
    { 66, "Tunnel-Client-Endpoint", NGX_RADIUS_TYPE_STRING  },
    { 67, "Tunnel-Server-Endpoint", NGX_RADIUS_TYPE_STRING  },
    { 68, "Acct-Tunnel-Connection", NGX_RADIUS_TYPE_OCTETS  },
    { 69, "Tunnel-Password",        NGX_RADIUS_TYPE_OCTETS  },
    { 70, "ARAP-Password",          NGX_RADIUS_TYPE_OCTETS  },
    { 71, "ARAP-Features",          NGX_RADIUS_TYPE_OCTETS  },
    { 72, "ARAP-Zone-Access",       NGX_RADIUS_TYPE_INTEGER },
    { 73, "ARAP-Security",          NGX_RADIUS_TYPE_INTEGER },
    { 74, "ARAP-Security-Data",     NGX_RADIUS_TYPE_OCTETS  },
    { 75, "Password-Retry",         NGX_RADIUS_TYPE_INTEGER },
    { 76, "Prompt",                 NGX_RADIUS_TYPE_INTEGER },
    { 77, "Connect-Info",           NGX_RADIUS_TYPE_STRING  },
    { 78, "Configuration-Token",    NGX_RADIUS_TYPE_OCTETS  },
    { 79, "EAP-Message",            NGX_RADIUS_TYPE_OCTETS  },
    { 80, "Message-Authenticator",  NGX_RADIUS_TYPE_OCTETS  },
    { 81, "Tunnel-Private-Group-Id",NGX_RADIUS_TYPE_OCTETS  },
    { 82, "Tunnel-Assignment-Id",   NGX_RADIUS_TYPE_OCTETS  },
    { 83, "Tunnel-Preference",      NGX_RADIUS_TYPE_INTEGER },
    { 84, "ARAP-Challenge-Response",NGX_RADIUS_TYPE_OCTETS  },
    { 85, "Acct-Interim-Interval",  NGX_RADIUS_TYPE_INTEGER },
    { 86, "Acct-Tunnel-Packets-Lost",NGX_RADIUS_TYPE_INTEGER},
    { 87, "NAS-Port-Id",            NGX_RADIUS_TYPE_STRING  },
    { 88, "Framed-Pool",            NGX_RADIUS_TYPE_OCTETS  },
    { 90, "Tunnel-Client-Auth-Id",  NGX_RADIUS_TYPE_OCTETS  },
    { 91, "Tunnel-Server-Auth-Id",  NGX_RADIUS_TYPE_OCTETS  },
    { 95, "NAS-IPv6-Address",       NGX_RADIUS_TYPE_IPV6ADDR},
    { 96, "Framed-Interface-Id",    NGX_RADIUS_TYPE_IFID    },
    { 97, "Framed-IPv6-Prefix",     NGX_RADIUS_TYPE_IPV6PREFIX},
    { 98, "Login-IPv6-Host",        NGX_RADIUS_TYPE_IPV6ADDR},
    { 99, "Framed-IPv6-Route",      NGX_RADIUS_TYPE_STRING  },
    {100, "Framed-IPv6-Pool",       NGX_RADIUS_TYPE_OCTETS  },
    {  0, NULL,                     0 }
};


/* -------------------------------------------------------------------------
 * Map data-type keyword string to NGX_RADIUS_TYPE_* constant
 * ---------------------------------------------------------------------- */

static ngx_uint_t
ngx_radius_parse_type_keyword(const char *kw)
{
    if (ngx_strcasecmp((u_char *) kw, (u_char *) "string")  == 0) return NGX_RADIUS_TYPE_STRING;
    if (ngx_strcasecmp((u_char *) kw, (u_char *) "integer") == 0) return NGX_RADIUS_TYPE_INTEGER;
    if (ngx_strcasecmp((u_char *) kw, (u_char *) "ipaddr")  == 0) return NGX_RADIUS_TYPE_IPADDR;
    if (ngx_strcasecmp((u_char *) kw, (u_char *) "date")    == 0) return NGX_RADIUS_TYPE_DATE;
    if (ngx_strcasecmp((u_char *) kw, (u_char *) "octets")  == 0) return NGX_RADIUS_TYPE_OCTETS;
    if (ngx_strcasecmp((u_char *) kw, (u_char *) "ipv6addr")== 0) return NGX_RADIUS_TYPE_IPV6ADDR;
    if (ngx_strcasecmp((u_char *) kw, (u_char *) "ipv6prefix")== 0)return NGX_RADIUS_TYPE_IPV6PREFIX;
    if (ngx_strcasecmp((u_char *) kw, (u_char *) "ifid")    == 0) return NGX_RADIUS_TYPE_IFID;
    if (ngx_strcasecmp((u_char *) kw, (u_char *) "integer64")== 0)return NGX_RADIUS_TYPE_INTEGER64;
    if (ngx_strcasecmp((u_char *) kw, (u_char *) "ether")   == 0) return NGX_RADIUS_TYPE_ETHER;
    if (ngx_strcasecmp((u_char *) kw, (u_char *) "abinary") == 0) return NGX_RADIUS_TYPE_OCTETS;
    if (ngx_strcasecmp((u_char *) kw, (u_char *) "byte")    == 0) return NGX_RADIUS_TYPE_INTEGER;
    if (ngx_strcasecmp((u_char *) kw, (u_char *) "short")   == 0) return NGX_RADIUS_TYPE_INTEGER;
    if (ngx_strcasecmp((u_char *) kw, (u_char *) "signed")  == 0) return NGX_RADIUS_TYPE_INTEGER;
    if (ngx_strcasecmp((u_char *) kw, (u_char *) "tlv")     == 0) return NGX_RADIUS_TYPE_OCTETS;
    if (ngx_strcasecmp((u_char *) kw, (u_char *) "extended") == 0)return NGX_RADIUS_TYPE_OCTETS;
    return NGX_RADIUS_TYPE_OCTETS; /* default fallback */
}


/* =========================================================================
 * Dictionary API
 * ====================================================================== */

ngx_radius_dict_t *
ngx_stream_radius_dict_create(ngx_pool_t *pool)
{
    ngx_radius_dict_t *dict;

    dict = ngx_pcalloc(pool, sizeof(ngx_radius_dict_t));
    if (dict == NULL) { return NULL; }

    dict->pool = pool;

    if (ngx_array_init(&dict->attrs, pool, 128,
                       sizeof(ngx_radius_attr_def_t)) != NGX_OK)
    { return NULL; }

    if (ngx_array_init(&dict->vendors, pool, 16,
                       sizeof(ngx_radius_vendor_def_t)) != NGX_OK)
    { return NULL; }

    return dict;
}


ngx_int_t
ngx_stream_radius_dict_load_builtins(ngx_radius_dict_t *dict)
{
    ngx_radius_builtin_attr_t  *ba;
    ngx_radius_attr_def_t      *def;

    for (ba = ngx_radius_builtin_attrs; ba->name != NULL; ba++) {
        def = ngx_array_push(&dict->attrs);
        if (def == NULL) { return NGX_ERROR; }

        def->type_code = ba->type_code;
        def->data_type = ba->data_type;
        def->vendor_id = 0;
        def->name.len  = ngx_strlen(ba->name);
        def->name.data = (u_char *) ba->name; /* static storage — safe */
    }

    return NGX_OK;
}


/* -------------------------------------------------------------------------
 * Look up a standard attribute by type code
 * ---------------------------------------------------------------------- */

ngx_radius_attr_def_t *
ngx_stream_radius_dict_lookup(ngx_radius_dict_t *dict, ngx_uint_t type_code)
{
    ngx_radius_attr_def_t *def;
    ngx_uint_t             i;

    if (dict == NULL) { return NULL; }

    def = dict->attrs.elts;
    for (i = 0; i < dict->attrs.nelts; i++) {
        if (def[i].type_code == type_code && def[i].vendor_id == 0) {
            return &def[i];
        }
    }
    return NULL;
}


/* -------------------------------------------------------------------------
 * Look up a vendor attribute by (vendor_id, vendor_type)
 * ---------------------------------------------------------------------- */

ngx_radius_attr_def_t *
ngx_stream_radius_dict_lookup_vsa(ngx_radius_dict_t *dict,
    ngx_uint_t vendor_id, ngx_uint_t vendor_type)
{
    ngx_radius_vendor_def_t *vendor;
    ngx_radius_attr_def_t   *vattr;
    ngx_uint_t               i, j;

    if (dict == NULL) { return NULL; }

    vendor = dict->vendors.elts;
    for (i = 0; i < dict->vendors.nelts; i++) {
        if (vendor[i].id != vendor_id) { continue; }

        vattr = vendor[i].attrs.elts;
        for (j = 0; j < vendor[i].attrs.nelts; j++) {
            if (vattr[j].type_code == vendor_type) {
                return &vattr[j];
            }
        }
    }
    return NULL;
}


/* -------------------------------------------------------------------------
 * Look up (or create) a vendor entry by PEN
 * ---------------------------------------------------------------------- */

static ngx_radius_vendor_def_t *
ngx_radius_dict_get_or_create_vendor(ngx_radius_dict_t *dict,
    ngx_uint_t vendor_id, const char *vendor_name)
{
    ngx_radius_vendor_def_t *vendor;
    ngx_uint_t               i;

    vendor = dict->vendors.elts;
    for (i = 0; i < dict->vendors.nelts; i++) {
        if (vendor[i].id == vendor_id) {
            return &vendor[i];
        }
    }

    /* Create new vendor entry */
    vendor = ngx_array_push(&dict->vendors);
    if (vendor == NULL) { return NULL; }

    ngx_memzero(vendor, sizeof(*vendor));
    vendor->id = vendor_id;

    if (vendor_name) {
        vendor->name.len  = ngx_strlen(vendor_name);
        vendor->name.data = (u_char *) ngx_pstrdup(dict->pool,
            &(ngx_str_t){ vendor->name.len, (u_char *) vendor_name });
    }

    if (ngx_array_init(&vendor->attrs, dict->pool, 16,
                       sizeof(ngx_radius_attr_def_t)) != NGX_OK)
    { return NULL; }

    return vendor;
}


/* =========================================================================
 * Dictionary file parser
 *
 * Understands the FreeRADIUS dictionary format:
 *
 *   VENDOR  <name>  <PEN>
 *   BEGIN-VENDOR <name>
 *   ATTRIBUTE  <name>  <type>  <datatype>  [vendor]
 *   VALUE  <attr>  <valname>  <number>
 *   $INCLUDE  <file>
 *   END-VENDOR <name>
 *   # comment
 * ====================================================================== */

#define NGX_RADIUS_DICT_MAX_LINE   512
#define NGX_RADIUS_DICT_MAX_TOKENS 8

static ngx_int_t
ngx_radius_dict_parse_line(ngx_radius_dict_t *dict, char *line,
    ngx_uint_t *current_vendor_id, ngx_log_t *log)
{
    char       *tokens[NGX_RADIUS_DICT_MAX_TOKENS];
    int         ntok;
    char       *p, *save;
    ngx_uint_t  attr_code, vendor_id;

    /* Trim comment and trailing whitespace */
    p = strchr(line, '#');
    if (p) { *p = '\0'; }

    /* Tokenise on whitespace */
    ntok = 0;
    p = strtok_r(line, " \t\r\n", &save);
    while (p && ntok < NGX_RADIUS_DICT_MAX_TOKENS) {
        tokens[ntok++] = p;
        p = strtok_r(NULL, " \t\r\n", &save);
    }

    if (ntok == 0) { return NGX_OK; } /* blank line */

    /* ---- VENDOR <name> <PEN> [format] ---- */
    if (ngx_strcasecmp((u_char *) tokens[0], (u_char *) "VENDOR") == 0) {
        if (ntok < 3) { return NGX_OK; }
        vendor_id = (ngx_uint_t) atoi(tokens[2]);
        if (ngx_radius_dict_get_or_create_vendor(dict, vendor_id,
                                                  tokens[1]) == NULL)
        { return NGX_ERROR; }
        return NGX_OK;
    }

    /* ---- BEGIN-VENDOR <name|PEN> ---- */
    if (ngx_strcasecmp((u_char *) tokens[0], (u_char *) "BEGIN-VENDOR") == 0) {
        if (ntok < 2) { return NGX_OK; }

        /* Try numeric PEN first */
        if (tokens[1][0] >= '0' && tokens[1][0] <= '9') {
            *current_vendor_id = (ngx_uint_t) atoi(tokens[1]);
        } else {
            /* Look up vendor by name */
            ngx_radius_vendor_def_t *vendor = dict->vendors.elts;
            ngx_uint_t i;
            *current_vendor_id = 0;
            for (i = 0; i < dict->vendors.nelts; i++) {
                if (ngx_strcasecmp(vendor[i].name.data,
                                   (u_char *) tokens[1]) == 0)
                {
                    *current_vendor_id = vendor[i].id;
                    break;
                }
            }
        }
        return NGX_OK;
    }

    /* ---- END-VENDOR ---- */
    if (ngx_strcasecmp((u_char *) tokens[0], (u_char *) "END-VENDOR") == 0) {
        *current_vendor_id = 0;
        return NGX_OK;
    }

    /* ---- ATTRIBUTE <name> <code> <type> [vendor] ---- */
    if (ngx_strcasecmp((u_char *) tokens[0], (u_char *) "ATTRIBUTE") == 0) {
        if (ntok < 4) { return NGX_OK; }

        attr_code = (ngx_uint_t) atoi(tokens[2]);

        /* Determine vendor context */
        vendor_id = *current_vendor_id;
        /* Some dictionaries specify vendor as 4th token */
        if (ntok >= 5 && tokens[4][0] >= '0' && tokens[4][0] <= '9') {
            vendor_id = (ngx_uint_t) atoi(tokens[4]);
        }

        if (vendor_id != 0) {
            ngx_radius_vendor_def_t *vendor;
            ngx_radius_attr_def_t   *def;

            vendor = ngx_radius_dict_get_or_create_vendor(dict, vendor_id, NULL);
            if (vendor == NULL) { return NGX_ERROR; }

            def = ngx_array_push(&vendor->attrs);
            if (def == NULL) { return NGX_ERROR; }

            ngx_memzero(def, sizeof(*def));
            def->type_code = attr_code;
            def->vendor_id = vendor_id;
            def->data_type = ngx_radius_parse_type_keyword(tokens[3]);
            def->name.len  = ngx_strlen(tokens[1]);
            def->name.data = ngx_pnalloc(dict->pool, def->name.len + 1);
            if (def->name.data == NULL) { return NGX_ERROR; }
            ngx_memcpy(def->name.data, tokens[1], def->name.len);
            def->name.data[def->name.len] = '\0';

        } else {
            ngx_radius_attr_def_t *def;

            def = ngx_array_push(&dict->attrs);
            if (def == NULL) { return NGX_ERROR; }

            ngx_memzero(def, sizeof(*def));
            def->type_code = attr_code;
            def->vendor_id = 0;
            def->data_type = ngx_radius_parse_type_keyword(tokens[3]);
            def->name.len  = ngx_strlen(tokens[1]);
            def->name.data = ngx_pnalloc(dict->pool, def->name.len + 1);
            if (def->name.data == NULL) { return NGX_ERROR; }
            ngx_memcpy(def->name.data, tokens[1], def->name.len);
            def->name.data[def->name.len] = '\0';
        }

        return NGX_OK;
    }

    /* VALUE and $INCLUDE lines are ignored for now (they only add enumeration
     * names for integer attributes, not needed for value extraction) */

    return NGX_OK;
}


ngx_int_t
ngx_stream_radius_dict_load_file(ngx_radius_dict_t *dict, ngx_str_t *path,
    ngx_log_t *log)
{
    FILE       *fp;
    char        line[NGX_RADIUS_DICT_MAX_LINE];
    char        path_cstr[NGX_MAX_PATH];
    ngx_uint_t  current_vendor_id = 0;
    ngx_uint_t  lineno = 0;

    if (path->len >= NGX_MAX_PATH) {
        ngx_log_error(NGX_LOG_EMERG, log, 0,
                      "radius: dictionary path too long: \"%V\"", path);
        return NGX_ERROR;
    }

    ngx_memcpy(path_cstr, path->data, path->len);
    path_cstr[path->len] = '\0';

    fp = fopen(path_cstr, "r");
    if (fp == NULL) {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                      "radius: fopen(\"%s\") failed", path_cstr);
        return NGX_ERROR;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        lineno++;

        if (ngx_radius_dict_parse_line(dict, line, &current_vendor_id,
                                        log) != NGX_OK)
        {
            ngx_log_error(NGX_LOG_WARN, log, 0,
                          "radius: error in dictionary \"%s\" line %d",
                          path_cstr, lineno);
        }
    }

    fclose(fp);

    ngx_log_error(NGX_LOG_INFO, log, 0,
                  "radius: loaded dictionary \"%s\" (%d lines)",
                  path_cstr, lineno);
    return NGX_OK;
}
