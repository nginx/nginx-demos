/*
 * test_radius_parser.c
 *
 * Standalone unit tests for the RADIUS parser and dictionary.
 * Compile independently (no NGINX headers needed) using the
 * provided test shim.
 *
 * Build:
 *   cd tests && make
 *
 * Copyright (C) 2024 - nginx-radius-module contributors
 * License: BSD 2-Clause
 */

#include "test_shim.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>


/* =========================================================================
 * Minimal RADIUS Access-Request packet (RFC 2865 §4)
 *
 * Code=1  Id=42  Len=53
 * Authenticator: 16 zero bytes (test only)
 * Attrs:
 *   User-Name (1): "testuser"   → type=1 len=10 value(8)
 *   NAS-IP-Address (4): 192.0.2.1 → type=4 len=6 value(4)
 *   NAS-Port (5): 1234          → type=5 len=6 value(4)
 * ====================================================================== */

static const uint8_t access_request[] = {
    /* Code=1, Id=42 */
    0x01, 0x2a,
    /* Length = 20 hdr + 10 + 6 + 6 = 42 */
    0x00, 0x2a,
    /* Authenticator (16 bytes) */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* Attr: User-Name(1) len=10 "testuser" */
    0x01, 0x0a,
    't',  'e',  's',  't',  'u',  's',  'e',  'r',
    /* Attr: NAS-IP-Address(4) len=6 192.0.2.1 */
    0x04, 0x06,
    0xc0, 0x00, 0x02, 0x01,
    /* Attr: NAS-Port(5) len=6 value=1234 */
    0x05, 0x06,
    0x00, 0x00, 0x04, 0xd2
};

/* Accounting-Request with Acct-Status-Type=Start (1) */
static const uint8_t accounting_request[] = {
    0x04, 0x01,          /* Code=4, Id=1 */
    0x00, 0x26,          /* Length=38 */
    /* Authenticator */
    0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11,
    0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
    /* Acct-Status-Type(40) len=6 value=1 */
    0x28, 0x06, 0x00, 0x00, 0x00, 0x01,
    /* Acct-Session-Id(44) len=12 "SESS-00001" */
    0x2c, 0x0c,
    'S',  'E',  'S',  'S',  '-',  '0',  '0',  '0',  '0',  '1'
};

/* Minimal VSA packet: Cisco-AVPair inside Vendor-Specific(26) */
static const uint8_t vsa_packet[] = {
    0x01, 0x07,           /* Code=1 (Access-Request), Id=7 */
    0x00, 0x26,           /* Length=38 */
    /* Authenticator */
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    /* Attr: Vendor-Specific(26), len=18 */
    0x1a, 0x12,
    /* Vendor-Id = 9 (Cisco) */
    0x00, 0x00, 0x00, 0x09,
    /* Sub-attr: Cisco-AVPair(1) len=12 "shell:priv-lvl=15" → shortened */
    0x01, 0x0c,
    's', 'h', 'e', 'l', 'l', ':', 'p', 'r', 'i', 'v'
};


/* =========================================================================
 * Helper assertion macros
 * ====================================================================== */

#define PASS(name) printf("[PASS] %s\n", (name))
#define FAIL(name, msg) do { \
    printf("[FAIL] %s: %s\n", (name), (msg)); \
    failures++; \
} while(0)

static int failures = 0;


/* =========================================================================
 * Test: parse Access-Request header
 * ====================================================================== */

static void
test_parse_access_request_header(void)
{
    test_radius_ctx_t ctx;
    test_radius_ctx_init(&ctx);

    int rc = test_parse_packet(&ctx, access_request, sizeof(access_request));
    assert(rc == 0);

    if (ctx.code != 1)
        FAIL("access_request_code", "code != 1");
    else
        PASS("access_request_code");

    if (ctx.identifier != 42)
        FAIL("access_request_id", "identifier != 42");
    else
        PASS("access_request_id");

    if (ctx.pkt_len != 42)
        FAIL("access_request_len", "pkt_len != 42");
    else
        PASS("access_request_len");
}


/* =========================================================================
 * Test: parse Access-Request attributes
 * ====================================================================== */

static void
test_parse_access_request_attrs(void)
{
    test_radius_ctx_t ctx;
    test_radius_ctx_init(&ctx);
    test_parse_packet(&ctx, access_request, sizeof(access_request));

    /* User-Name */
    const char *username = test_find_attr(&ctx, 1, 0, 0);
    if (username == NULL || strcmp(username, "testuser") != 0)
        FAIL("attr_user_name", "User-Name mismatch");
    else
        PASS("attr_user_name");

    /* NAS-IP-Address */
    const char *nas_ip = test_find_attr(&ctx, 4, 0, 0);
    if (nas_ip == NULL || strcmp(nas_ip, "192.0.2.1") != 0)
        FAIL("attr_nas_ip", "NAS-IP-Address mismatch");
    else
        PASS("attr_nas_ip");

    /* NAS-Port = 1234 */
    const char *nas_port = test_find_attr(&ctx, 5, 0, 0);
    if (nas_port == NULL || strcmp(nas_port, "1234") != 0)
        FAIL("attr_nas_port", "NAS-Port mismatch");
    else
        PASS("attr_nas_port");
}


/* =========================================================================
 * Test: parse Accounting-Request
 * ====================================================================== */

static void
test_parse_accounting_request(void)
{
    test_radius_ctx_t ctx;
    test_radius_ctx_init(&ctx);

    int rc = test_parse_packet(&ctx, accounting_request,
                               sizeof(accounting_request));
    assert(rc == 0);

    if (ctx.code != 4)
        FAIL("acct_code", "code != 4 (Accounting-Request)");
    else
        PASS("acct_code");

    /* Acct-Status-Type = 1 */
    const char *status = test_find_attr(&ctx, 40, 0, 0);
    if (status == NULL || strcmp(status, "1") != 0)
        FAIL("acct_status_type", "Acct-Status-Type mismatch");
    else
        PASS("acct_status_type");

    /* Acct-Session-Id */
    const char *session_id = test_find_attr(&ctx, 44, 0, 0);
    if (session_id == NULL || strncmp(session_id, "SESS-0000", 9) != 0)
        FAIL("acct_session_id", "Acct-Session-Id mismatch");
    else
        PASS("acct_session_id");
}


/* =========================================================================
 * Test: parse VSA (Cisco)
 * ====================================================================== */

static void
test_parse_vsa(void)
{
    test_radius_ctx_t ctx;
    test_radius_ctx_init(&ctx);

    int rc = test_parse_packet(&ctx, vsa_packet, sizeof(vsa_packet));
    assert(rc == 0);

    /* Find Cisco-AVPair (vendor=9, sub-type=1) */
    const char *avpair = test_find_attr(&ctx, 26, 9, 1);
    if (avpair == NULL)
        FAIL("vsa_cisco_avpair", "Cisco-AVPair not found");
    else
        PASS("vsa_cisco_avpair");
}


/* =========================================================================
 * Test: truncated / malformed packet rejection
 * ====================================================================== */

static void
test_malformed_packet(void)
{
    test_radius_ctx_t ctx;
    int rc;

    /* Too short: only 5 bytes */
    static const uint8_t too_short[] = { 0x01, 0x01, 0x00, 0x05, 0x00 };
    test_radius_ctx_init(&ctx);
    rc = test_parse_packet(&ctx, too_short, sizeof(too_short));
    if (rc == 0)
        FAIL("malformed_too_short", "should have rejected short packet");
    else
        PASS("malformed_too_short");

    /* Length field larger than actual buffer */
    static const uint8_t bad_len[] = {
        0x01, 0x01, 0x01, 0x00, /* code=1 id=1 len=256 */
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    };
    test_radius_ctx_init(&ctx);
    rc = test_parse_packet(&ctx, bad_len, 20); /* only pass 20 bytes */
    if (rc == 0)
        FAIL("malformed_bad_len", "should have rejected bad length");
    else
        PASS("malformed_bad_len");
}


/* =========================================================================
 * Test: dictionary file loading
 * ====================================================================== */

static void
test_dict_load(void)
{
    test_dict_t dict;
    test_dict_init(&dict);

    int rc = test_dict_load_file(&dict, "dict/dictionary.cisco");
    if (rc != 0) {
        FAIL("dict_load_cisco", "failed to load cisco dictionary");
        return;
    }
    PASS("dict_load_cisco");

    /* Cisco-AVPair should be type 1 under vendor 9 */
    test_attr_def_t *def = test_dict_lookup_vsa(&dict, 9, 1);
    if (def == NULL)
        FAIL("dict_cisco_avpair", "Cisco-AVPair not found in dict");
    else if (strcmp(def->name, "Cisco-AVPair") != 0)
        FAIL("dict_cisco_avpair_name", "Cisco-AVPair name mismatch");
    else
        PASS("dict_cisco_avpair");

    test_dict_destroy(&dict);
}


/* =========================================================================
 * main
 * ====================================================================== */

int main(void)
{
    printf("=== ngx_stream_radius_module unit tests ===\n\n");

    test_parse_access_request_header();
    test_parse_access_request_attrs();
    test_parse_accounting_request();
    test_parse_vsa();
    test_malformed_packet();
    test_dict_load();

    printf("\n=== Results: %d failure(s) ===\n", failures);
    return failures == 0 ? 0 : 1;
}
