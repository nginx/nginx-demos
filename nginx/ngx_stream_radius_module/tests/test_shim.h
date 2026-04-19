/*
 * test_shim.h
 *
 * Portable shim that re-implements the minimal subset of NGINX types and
 * calls needed to compile and exercise the RADIUS parser outside of a
 * full NGINX build.  This allows fast iteration on parse logic without a
 * complete NGINX source tree.
 *
 * Copyright (C) 2024 - nginx-radius-module contributors
 * License: BSD 2-Clause
 */

#ifndef TEST_SHIM_H
#define TEST_SHIM_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>
#include <arpa/inet.h>

/* -------------------------------------------------------------------------
 * Minimal NGINX type stubs
 * ---------------------------------------------------------------------- */

typedef unsigned char  u_char;
typedef uintptr_t      ngx_uint_t;
typedef intptr_t       ngx_int_t;
typedef size_t         ngx_size_t;

#define NGX_OK     0
#define NGX_ERROR -1

/* -------------------------------------------------------------------------
 * Attribute representation (mirrors ngx_stream_radius_attr_t)
 * ---------------------------------------------------------------------- */

#define TEST_MAX_ATTRS 255

typedef struct {
    uint32_t    type;
    uint32_t    vendor_id;
    uint32_t    vendor_type;
    char        value[512];
    uint8_t     raw[256];
    size_t      raw_len;
    uint32_t    data_type;
} test_attr_t;

typedef struct {
    uint8_t     code;
    uint8_t     identifier;
    uint16_t    pkt_len;
    uint8_t     authenticator[16];
    test_attr_t attrs[TEST_MAX_ATTRS];
    int         nattrs;
    int         parse_error;
} test_radius_ctx_t;

static inline void
test_radius_ctx_init(test_radius_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
}

/* -------------------------------------------------------------------------
 * Attribute data types (same values as NGX_RADIUS_TYPE_*)
 * ---------------------------------------------------------------------- */

#define TEST_TYPE_STRING    1
#define TEST_TYPE_INTEGER   2
#define TEST_TYPE_IPADDR    3
#define TEST_TYPE_DATE      4
#define TEST_TYPE_OCTETS    5
#define TEST_TYPE_IPV6ADDR  6

/* -------------------------------------------------------------------------
 * Helper: decode attribute value to string
 * ---------------------------------------------------------------------- */

static inline void
test_decode_value(test_attr_t *attr, const uint8_t *data, size_t len,
                  uint32_t dtype)
{
    struct in_addr  a4;
    struct in6_addr a6;

    memcpy(attr->raw, data, len < sizeof(attr->raw) ? len : sizeof(attr->raw));
    attr->raw_len  = len;
    attr->data_type = dtype;

    switch (dtype) {
    case TEST_TYPE_INTEGER:
    case TEST_TYPE_DATE:
        if (len == 4) {
            uint32_t v = ((uint32_t)data[0] << 24) |
                         ((uint32_t)data[1] << 16) |
                         ((uint32_t)data[2] << 8)  |
                          (uint32_t)data[3];
            snprintf(attr->value, sizeof(attr->value), "%u", v);
        }
        break;
    case TEST_TYPE_IPADDR:
        if (len == 4) {
            memcpy(&a4.s_addr, data, 4);
            inet_ntop(AF_INET, &a4, attr->value, sizeof(attr->value));
        }
        break;
    case TEST_TYPE_IPV6ADDR:
        if (len == 16) {
            memcpy(&a6, data, 16);
            inet_ntop(AF_INET6, &a6, attr->value, sizeof(attr->value));
        }
        break;
    case TEST_TYPE_STRING:
        memcpy(attr->value, data, len < sizeof(attr->value)-1 ? len : sizeof(attr->value)-1);
        attr->value[len < sizeof(attr->value)-1 ? len : sizeof(attr->value)-1] = '\0';
        break;
    default: {
        char *p = attr->value;
        p += snprintf(p, sizeof(attr->value), "0x");
        for (size_t i = 0; i < len && i < 64; i++) {
            p += snprintf(p, sizeof(attr->value) - (p - attr->value),
                          "%02x", data[i]);
        }
        break;
    }
    }
}


/* -------------------------------------------------------------------------
 * Minimal packet parser (mirrors the production parser logic)
 * ---------------------------------------------------------------------- */

static inline int
test_parse_packet(test_radius_ctx_t *ctx, const uint8_t *pkt, size_t len)
{
#define RADIUS_HEADER_LEN 20
#define RADIUS_VSA_TYPE   26
#define RADIUS_MAX_LEN    4096

    if (len < RADIUS_HEADER_LEN) {
        ctx->parse_error = 1;
        return -1;
    }

    ctx->code       = pkt[0];
    ctx->identifier = pkt[1];
    ctx->pkt_len    = ((uint16_t)pkt[2] << 8) | pkt[3];

    if (ctx->pkt_len < RADIUS_HEADER_LEN || ctx->pkt_len > RADIUS_MAX_LEN
        || ctx->pkt_len > (uint16_t)len)
    {
        ctx->parse_error = 1;
        return -1;
    }

    memcpy(ctx->authenticator, pkt + 4, 16);

    const uint8_t *p   = pkt + RADIUS_HEADER_LEN;
    const uint8_t *end = pkt + ctx->pkt_len;

    while (p < end) {
        if (p + 2 > end) { ctx->parse_error = 1; return -1; }
        uint8_t atype = p[0];
        uint8_t alen  = p[1];
        if (alen < 2 || p + alen > end) { ctx->parse_error = 1; return -1; }

        if (atype == RADIUS_VSA_TYPE) {
            /* Parse VSA sub-attributes */
            if (alen < 8) { p += alen; continue; }
            uint32_t vid = ((uint32_t)p[2] << 24) | ((uint32_t)p[3] << 16) |
                           ((uint32_t)p[4] << 8)  |  (uint32_t)p[5];
            const uint8_t *vp  = p + 6;
            const uint8_t *vend= p + alen;
            while (vp + 2 <= vend) {
                uint8_t vtype = vp[0];
                uint8_t vlen  = vp[1];
                if (vlen < 2 || vp + vlen > vend) break;
                if (ctx->nattrs < TEST_MAX_ATTRS) {
                    test_attr_t *a = &ctx->attrs[ctx->nattrs++];
                    memset(a, 0, sizeof(*a));
                    a->type       = RADIUS_VSA_TYPE;
                    a->vendor_id  = vid;
                    a->vendor_type= vtype;
                    test_decode_value(a, vp + 2, vlen - 2, TEST_TYPE_OCTETS);
                }
                vp += vlen;
            }
        } else if (ctx->nattrs < TEST_MAX_ATTRS) {
            test_attr_t *a = &ctx->attrs[ctx->nattrs++];
            memset(a, 0, sizeof(*a));
            a->type      = atype;
            a->vendor_id = 0;
            /* Basic type guessing for test purposes */
            uint32_t dtype = TEST_TYPE_OCTETS;
            if (atype == 1 || atype == 11 || atype == 18 ||
                atype == 30 || atype == 31 || atype == 32 ||
                atype == 44 || atype == 77 || atype == 87) {
                dtype = TEST_TYPE_STRING;
            } else if (atype == 4 || atype == 8 || atype == 9 ||
                       atype == 14) {
                dtype = TEST_TYPE_IPADDR;
            } else if (atype == 5  || atype == 6  || atype == 7  ||
                       atype == 10 || atype == 12 || atype == 13 ||
                       atype == 27 || atype == 28 || atype == 40 ||
                       atype == 41 || atype == 42 || atype == 43 ||
                       atype == 45 || atype == 46 || atype == 47 ||
                       atype == 48 || atype == 49 || atype == 61) {
                dtype = TEST_TYPE_INTEGER;
            }
            test_decode_value(a, p + 2, alen - 2, dtype);
        }
        p += alen;
    }
    return 0;
}


/* -------------------------------------------------------------------------
 * Find a parsed attribute
 * ---------------------------------------------------------------------- */

static inline const char *
test_find_attr(test_radius_ctx_t *ctx, uint32_t type,
               uint32_t vendor_id, uint32_t vendor_type)
{
    for (int i = 0; i < ctx->nattrs; i++) {
        test_attr_t *a = &ctx->attrs[i];
        if (a->type == type && a->vendor_id == vendor_id) {
            if (vendor_id == 0 || a->vendor_type == vendor_type)
                return a->value;
        }
    }
    return NULL;
}


/* -------------------------------------------------------------------------
 * Minimal dictionary for tests
 * ---------------------------------------------------------------------- */

typedef struct {
    uint32_t  vendor_id;
    uint32_t  type_code;
    char      name[64];
    uint32_t  data_type;
} test_attr_def_t;

#define TEST_MAX_DEFS 1024

typedef struct {
    test_attr_def_t defs[TEST_MAX_DEFS];
    int             ndefs;
} test_dict_t;

static inline void
test_dict_init(test_dict_t *d) { memset(d, 0, sizeof(*d)); }

static inline void
test_dict_destroy(test_dict_t *d) { (void)d; }

static inline test_attr_def_t *
test_dict_lookup_vsa(test_dict_t *d, uint32_t vendor_id, uint32_t type_code)
{
    for (int i = 0; i < d->ndefs; i++) {
        if (d->defs[i].vendor_id == vendor_id &&
            d->defs[i].type_code == type_code)
            return &d->defs[i];
    }
    return NULL;
}

static inline int
test_dict_load_file(test_dict_t *d, const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        /* Try relative to parent directory (for CI runs from repo root) */
        char buf[512];
        snprintf(buf, sizeof(buf), "../%s", path);
        fp = fopen(buf, "r");
        if (!fp) return -1;
    }

    char     line[512];
    uint32_t current_vendor = 0;

    while (fgets(line, sizeof(line), fp)) {
        char *p = strchr(line, '#');
        if (p) *p = '\0';

        char tok0[64], tok1[64], tok2[64], tok3[64];
        int n = sscanf(line, "%63s %63s %63s %63s", tok0, tok1, tok2, tok3);
        if (n < 1) continue;

        if (strcasecmp(tok0, "VENDOR") == 0 && n >= 3) {
            /* VENDOR <name> <PEN> */
            /* just register the mapping */
        } else if (strcasecmp(tok0, "BEGIN-VENDOR") == 0 && n >= 2) {
            if (tok1[0] >= '0' && tok1[0] <= '9')
                current_vendor = (uint32_t) atoi(tok1);
            else {
                /* resolve by scanning already-loaded VENDOR declarations */
                /* for test purposes, use Cisco=9 and Mikrotik=14988 hardcoded */
                if (strcasecmp(tok1, "Cisco") == 0) current_vendor = 9;
                else if (strcasecmp(tok1, "Mikrotik") == 0) current_vendor = 14988;
                else current_vendor = 0;
            }
        } else if (strcasecmp(tok0, "END-VENDOR") == 0) {
            current_vendor = 0;
        } else if (strcasecmp(tok0, "ATTRIBUTE") == 0 && n >= 4) {
            if (d->ndefs >= TEST_MAX_DEFS) continue;
            test_attr_def_t *def = &d->defs[d->ndefs++];
            memset(def, 0, sizeof(*def));
            def->vendor_id  = current_vendor;
            def->type_code  = (uint32_t) atoi(tok2);
            strncpy(def->name, tok1, sizeof(def->name) - 1);
            def->data_type = TEST_TYPE_OCTETS;
            if (strcasecmp(tok3, "string")  == 0) def->data_type = TEST_TYPE_STRING;
            if (strcasecmp(tok3, "integer") == 0) def->data_type = TEST_TYPE_INTEGER;
            if (strcasecmp(tok3, "ipaddr")  == 0) def->data_type = TEST_TYPE_IPADDR;
        }
    }
    fclose(fp);
    return 0;
}

#endif /* TEST_SHIM_H */
