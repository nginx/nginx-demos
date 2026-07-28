#ifndef NGX_DNS_PARSER_H
#define NGX_DNS_PARSER_H

#include <stdint.h>
#include <stddef.h>

#define NGX_DNS_MAX_NAME_LEN 256
#define NGX_DNS_MAX_RECORDS 32
#define NGX_DNS_MAX_RDATA_STR 1024
#define NGX_DNS_MAX_POINTER_JUMPS 10

/* DNS Record Types (RFC 1035, RFC 3596, etc.) */
#define NGX_DNS_TYPE_A     1
#define NGX_DNS_TYPE_NS    2
#define NGX_DNS_TYPE_CNAME 5
#define NGX_DNS_TYPE_SOA   6
#define NGX_DNS_TYPE_PTR   12
#define NGX_DNS_TYPE_MX    15
#define NGX_DNS_TYPE_TXT   16
#define NGX_DNS_TYPE_AAAA  28
#define NGX_DNS_TYPE_SRV   33
#define NGX_DNS_TYPE_OPT   41
#define NGX_DNS_TYPE_CAA   257
#define NGX_DNS_TYPE_ANY   255

/* DNS Classes */
#define NGX_DNS_CLASS_IN   1
#define NGX_DNS_CLASS_CS   2
#define NGX_DNS_CLASS_CH   3
#define NGX_DNS_CLASS_HS   4
#define NGX_DNS_CLASS_ANY  255

/* DNS Response Codes (RCODE) */
#define NGX_DNS_RCODE_NOERROR  0
#define NGX_DNS_RCODE_FORMERR  1
#define NGX_DNS_RCODE_SERVFAIL 2
#define NGX_DNS_RCODE_NXDOMAIN 3
#define NGX_DNS_RCODE_NOTIMP   4
#define NGX_DNS_RCODE_REFUSED  5

typedef struct {
    uint16_t id;
    uint16_t flags;
    uint8_t  qr;
    uint8_t  opcode;
    uint8_t  aa;
    uint8_t  tc;
    uint8_t  rd;
    uint8_t  ra;
    uint8_t  z;
    uint8_t  rcode;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} ngx_dns_header_t;

typedef struct {
    char     qname[NGX_DNS_MAX_NAME_LEN];
    uint16_t qtype;
    uint16_t qclass;
    const char *qtype_str;
    const char *qclass_str;
} ngx_dns_question_t;

typedef struct {
    char     name[NGX_DNS_MAX_NAME_LEN];
    uint16_t type;
    uint16_t rr_class;
    uint32_t ttl;
    uint16_t rdlength;
    char     rdata_str[NGX_DNS_MAX_RDATA_STR];
    char     ip_str[64]; /* Formatted IP address if A or AAAA */
    const char *type_str;
} ngx_dns_rr_t;

typedef struct {
    ngx_dns_header_t   header;
    ngx_dns_question_t question;
    ngx_dns_rr_t       answers[NGX_DNS_MAX_RECORDS];
    ngx_dns_rr_t       authorities[NGX_DNS_MAX_RECORDS];
    ngx_dns_rr_t       additionals[NGX_DNS_MAX_RECORDS];
    uint16_t           parsed_answers;
    uint16_t           parsed_authorities;
    uint16_t           parsed_additionals;
    int                parsed_ok;
    const char        *error_msg;
} ngx_dns_packet_t;

/* Function Prototypes */
int ngx_dns_parse_packet(const uint8_t *buf, size_t len, int is_tcp, ngx_dns_packet_t *pkt);
const char *ngx_dns_type_to_str(uint16_t type);
const char *ngx_dns_class_to_str(uint16_t class_code);
const char *ngx_dns_rcode_to_str(uint8_t rcode);

size_t ngx_dns_packet_to_json(const ngx_dns_packet_t *pkt, char *json_buf, size_t max_len);
size_t ngx_dns_answers_to_ips(const ngx_dns_packet_t *pkt, char *ip_buf, size_t max_len);

#endif /* NGX_DNS_PARSER_H */
