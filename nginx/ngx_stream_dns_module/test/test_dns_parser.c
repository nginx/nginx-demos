#include "../src/ngx_dns_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Colors for test output */
#define GREEN "\033[0;32m"
#define RED   "\033[0;31m"
#define RESET "\033[0m"

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define RUN_TEST(fn) do { \
    g_tests_run++; \
    printf("Running %s... ", #fn); \
    if (fn() == 0) { \
        printf(GREEN "PASSED" RESET "\n"); \
        g_tests_passed++; \
    } else { \
        printf(RED "FAILED" RESET "\n"); \
    } \
} while(0)

/* Test 1: UDP Query for A record example.com */
static int test_udp_query_a_record(void) {
    uint8_t wire_packet[] = {
        0x12, 0x34,               /* ID */
        0x01, 0x00,               /* Flags: Standard Query, RD=1 */
        0x00, 0x01,               /* QDCOUNT = 1 */
        0x00, 0x00,               /* ANCOUNT = 0 */
        0x00, 0x00,               /* NSCOUNT = 0 */
        0x00, 0x00,               /* ARCOUNT = 0 */
        /* QNAME: 7example3com0 */
        0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
        0x03, 'c', 'o', 'm',
        0x00,
        0x00, 0x01,               /* QTYPE = A (1) */
        0x00, 0x01                /* QCLASS = IN (1) */
    };

    ngx_dns_packet_t pkt;
    int rc = ngx_dns_parse_packet(wire_packet, sizeof(wire_packet), 0, &pkt);

    assert(rc == 0);
    assert(pkt.parsed_ok == 1);
    assert(pkt.header.id == 0x1234);
    assert(pkt.header.qr == 0);
    assert(pkt.header.qdcount == 1);
    assert(strcmp(pkt.question.qname, "example.com") == 0);
    assert(pkt.question.qtype == NGX_DNS_TYPE_A);

    return 0;
}

/* Test 2: TCP Length Prefixed Query */
static int test_tcp_query_a_record(void) {
    uint8_t tcp_packet[] = {
        0x00, 0x1D,               /* TCP 2-byte Length prefix = 29 bytes */
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 0x03, 'c', 'o', 'm', 0x00,
        0x00, 0x01, 0x00, 0x01
    };

    ngx_dns_packet_t pkt;
    int rc = ngx_dns_parse_packet(tcp_packet, sizeof(tcp_packet), 1, &pkt);

    assert(rc == 0);
    assert(pkt.parsed_ok == 1);
    assert(pkt.header.id == 0x1234);
    assert(strcmp(pkt.question.qname, "example.com") == 0);

    return 0;
}

/* Test 3: Response for A record example.com */
static int test_response_a_record(void) {
    uint8_t wire_packet[] = {
        0xAB, 0xCD, 0x81, 0x80,   /* ID = 0xABCD, Flags = Standard Response, NoError */
        0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
        /* Question */
        0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 0x03, 'c', 'o', 'm', 0x00,
        0x00, 0x01, 0x00, 0x01,
        /* Answer Section */
        0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01,
        0x00, 0x00, 0x01, 0x2C,   /* TTL = 300 */
        0x00, 0x04,
        93, 184, 216, 34
    };

    ngx_dns_packet_t pkt;
    int rc = ngx_dns_parse_packet(wire_packet, sizeof(wire_packet), 0, &pkt);

    assert(rc == 0);
    assert(pkt.parsed_ok == 1);
    assert(pkt.header.id == 0xABCD);
    assert(pkt.parsed_answers == 1);
    assert(strcmp(pkt.answers[0].ip_str, "93.184.216.34") == 0);

    return 0;
}

/* Test 4: Truncated Packet Handling */
static int test_truncated_packet(void) {
    uint8_t short_packet[] = { 0x12, 0x34, 0x01 };

    ngx_dns_packet_t pkt;
    int rc = ngx_dns_parse_packet(short_packet, sizeof(short_packet), 0, &pkt);

    assert(rc == -1);
    assert(pkt.parsed_ok == 0);

    return 0;
}

/* Test 5: Pointer Loop Attack Protection */
static int test_pointer_loop(void) {
    uint8_t loop_packet[] = {
        0x12, 0x34, 0x01, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xC0, 0x0C,
        0x00, 0x01, 0x00, 0x01
    };

    ngx_dns_packet_t pkt;
    int rc = ngx_dns_parse_packet(loop_packet, sizeof(loop_packet), 0, &pkt);

    assert(rc == -1);
    assert(pkt.parsed_ok == 0);

    return 0;
}

int main(void) {
    printf("=== Starting Stream DNS Parser Unit Tests ===\n\n");

    RUN_TEST(test_udp_query_a_record);
    RUN_TEST(test_tcp_query_a_record);
    RUN_TEST(test_response_a_record);
    RUN_TEST(test_truncated_packet);
    RUN_TEST(test_pointer_loop);

    printf("\n=== Summary: %d/%d tests passed ===\n", g_tests_passed, g_tests_run);

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
