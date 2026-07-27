#include "ngx_dns_parser.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Portable Big-Endian (Network Byte Order) helpers */
static inline uint16_t ngx_dns_read_u16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static inline uint32_t ngx_dns_read_u32(const uint8_t *p) {
    return (((uint32_t)p[0] << 24) |
            ((uint32_t)p[1] << 16) |
            ((uint32_t)p[2] << 8)  |
             (uint32_t)p[3]);
}

const char *ngx_dns_type_to_str(uint16_t type) {
    switch (type) {
        case NGX_DNS_TYPE_A:     return "A";
        case NGX_DNS_TYPE_NS:    return "NS";
        case NGX_DNS_TYPE_CNAME: return "CNAME";
        case NGX_DNS_TYPE_SOA:   return "SOA";
        case NGX_DNS_TYPE_PTR:   return "PTR";
        case NGX_DNS_TYPE_MX:    return "MX";
        case NGX_DNS_TYPE_TXT:   return "TXT";
        case NGX_DNS_TYPE_AAAA:  return "AAAA";
        case NGX_DNS_TYPE_SRV:   return "SRV";
        case NGX_DNS_TYPE_OPT:   return "OPT";
        case NGX_DNS_TYPE_CAA:   return "CAA";
        case NGX_DNS_TYPE_ANY:   return "ANY";
        default:                 return "UNKNOWN";
    }
}

const char *ngx_dns_class_to_str(uint16_t class_code) {
    switch (class_code) {
        case NGX_DNS_CLASS_IN:  return "IN";
        case NGX_DNS_CLASS_CS:  return "CS";
        case NGX_DNS_CLASS_CH:  return "CH";
        case NGX_DNS_CLASS_HS:  return "HS";
        case NGX_DNS_CLASS_ANY: return "ANY";
        default:                return "UNKNOWN";
    }
}

const char *ngx_dns_rcode_to_str(uint8_t rcode) {
    switch (rcode) {
        case NGX_DNS_RCODE_NOERROR:  return "NOERROR";
        case NGX_DNS_RCODE_FORMERR:  return "FORMERR";
        case NGX_DNS_RCODE_SERVFAIL: return "SERVFAIL";
        case NGX_DNS_RCODE_NXDOMAIN: return "NXDOMAIN";
        case NGX_DNS_RCODE_NOTIMP:   return "NOTIMP";
        case NGX_DNS_RCODE_REFUSED:  return "REFUSED";
        default:                     return "UNKNOWN";
    }
}

/* Parse domain name with pointer loop protection and bounds checking.
 * Returns bytes consumed from the initial stream position (not counting pointer jumps).
 */
static int ngx_dns_parse_name(const uint8_t *buf, size_t buf_len, size_t offset,
                             char *out_name, size_t out_max, size_t *bytes_read) {
    size_t pos = offset;
    size_t out_pos = 0;
    int jumps = 0;
    int initial_bytes_counted = 0;
    size_t consumed = 0;

    if (out_name == NULL || out_max == 0) return -1;
    out_name[0] = '\0';

    while (pos < buf_len) {
        uint8_t len = buf[pos];

        /* Check for domain compression pointer (top 2 bits set: 11xxxxxx) */
        if ((len & 0xC0) == 0xC0) {
            if (pos + 1 >= buf_len) {
                return -1; /* Truncated pointer */
            }
            uint16_t ptr = (uint16_t)(((len & 0x3F) << 8) | buf[pos + 1]);

            if (!initial_bytes_counted) {
                consumed += 2;
                initial_bytes_counted = 1;
            }

            pos = ptr;
            jumps++;
            if (jumps > NGX_DNS_MAX_POINTER_JUMPS) {
                return -1; /* Pointer loop / excessive recursion */
            }
            continue;
        }

        /* End of domain name label */
        if (len == 0) {
            if (!initial_bytes_counted) {
                consumed += 1;
            }
            pos++;
            break;
        }

        /* Standard label length check */
        if (len > 63 || pos + 1 + len > buf_len) {
            return -1; /* Invalid label length or buffer overrun */
        }

        if (!initial_bytes_counted) {
            consumed += (1 + len);
        }

        /* Append dot separator if not the first label */
        if (out_pos > 0) {
            if (out_pos + 1 >= out_max) return -1;
            out_name[out_pos++] = '.';
        }

        /* Copy label content */
        pos++;
        for (uint8_t i = 0; i < len; i++) {
            if (out_pos + 1 >= out_max) return -1;
            out_name[out_pos++] = (char)buf[pos++];
        }
    }

    out_name[out_pos] = '\0';

    if (bytes_read) {
        *bytes_read = consumed;
    }

    return 0;
}

#if defined(__GNUC__) && (__GNUC__ >= 7)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

static int ngx_dns_parse_rdata(const uint8_t *buf, size_t buf_len, size_t rdata_offset,
                               uint16_t type, uint16_t rdlength, ngx_dns_rr_t *rr) {
    if (rdata_offset + rdlength > buf_len) return -1;

    switch (type) {
        case NGX_DNS_TYPE_A:
            if (rdlength == 4) {
                const uint8_t *p = buf + rdata_offset;
                snprintf(rr->ip_str, sizeof(rr->ip_str), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
            }
            break;

        case NGX_DNS_TYPE_AAAA:
            if (rdlength == 16) {
                const uint8_t *p = buf + rdata_offset;
                uint16_t words[8];
                for (int i = 0; i < 8; i++) {
                    words[i] = (p[i*2] << 8) | p[i*2 + 1];
                }
                snprintf(rr->ip_str, sizeof(rr->ip_str),
                         "%x:%x:%x:%x:%x:%x:%x:%x",
                         words[0], words[1], words[2], words[3],
                         words[4], words[5], words[6], words[7]);
            }
            break;

        case NGX_DNS_TYPE_CNAME:
        case NGX_DNS_TYPE_NS:
        case NGX_DNS_TYPE_PTR:
            {
                size_t name_read = 0;
                ngx_dns_parse_name(buf, buf_len, rdata_offset, rr->rdata_str, sizeof(rr->rdata_str), &name_read);
            }
            break;

        case NGX_DNS_TYPE_MX:
            if (rdlength >= 3) {
                uint16_t pref = ngx_dns_read_u16(buf + rdata_offset);
                char exchange[NGX_DNS_MAX_NAME_LEN];
                size_t name_read = 0;
                if (ngx_dns_parse_name(buf, buf_len, rdata_offset + 2, exchange, sizeof(exchange), &name_read) == 0) {
                    snprintf(rr->rdata_str, sizeof(rr->rdata_str), "%u %.255s", pref, exchange);
                }
            }
            break;

        case NGX_DNS_TYPE_TXT:
            if (rdlength > 0) {
                uint8_t txt_len = buf[rdata_offset];
                size_t copy_len = (txt_len < rdlength - 1) ? txt_len : rdlength - 1;
                size_t out_len = (copy_len < sizeof(rr->rdata_str) - 1) ? copy_len : sizeof(rr->rdata_str) - 1;
                memcpy(rr->rdata_str, buf + rdata_offset + 1, out_len);
                rr->rdata_str[out_len] = '\0';
            }
            break;

        case NGX_DNS_TYPE_SOA:
            {
                char mname[NGX_DNS_MAX_NAME_LEN];
                char rname[NGX_DNS_MAX_NAME_LEN];
                size_t read1 = 0, read2 = 0;

                if (ngx_dns_parse_name(buf, buf_len, rdata_offset, mname, sizeof(mname), &read1) == 0 &&
                    ngx_dns_parse_name(buf, buf_len, rdata_offset + read1, rname, sizeof(rname), &read2) == 0) {
                    size_t num_offset = rdata_offset + read1 + read2;
                    if (num_offset + 20 <= rdata_offset + rdlength) {
                        uint32_t serial  = ngx_dns_read_u32(buf + num_offset);
                        uint32_t refresh = ngx_dns_read_u32(buf + num_offset + 4);
                        uint32_t retry   = ngx_dns_read_u32(buf + num_offset + 8);
                        uint32_t expire  = ngx_dns_read_u32(buf + num_offset + 12);
                        uint32_t minimum = ngx_dns_read_u32(buf + num_offset + 16);

                        char nums[128];
                        snprintf(nums, sizeof(nums), "%u %u %u %u %u", serial, refresh, retry, expire, minimum);

                        size_t len = 0;
                        size_t max = sizeof(rr->rdata_str);
                        len += snprintf(rr->rdata_str + len, (len < max) ? max - len : 0, "%.200s %.200s ", mname, rname);
                        if (len < max) {
                            snprintf(rr->rdata_str + len, max - len, "%s", nums);
                        }
                    }
                }
            }
            break;

        default:
            {
                size_t written = 0;
                for (size_t i = 0; i < rdlength && written + 3 < sizeof(rr->rdata_str); i++) {
                    written += snprintf(rr->rdata_str + written, sizeof(rr->rdata_str) - written,
                                        "%02x", buf[rdata_offset + i]);
                }
            }
            break;
    }

    return 0;
}

#if defined(__GNUC__) && (__GNUC__ >= 7)
#pragma GCC diagnostic pop
#endif

int ngx_dns_parse_packet(const uint8_t *buf, size_t len, int is_tcp, ngx_dns_packet_t *pkt) {
    if (buf == NULL || pkt == NULL) return -1;

    memset(pkt, 0, sizeof(ngx_dns_packet_t));

    /* Check for TCP 2-byte length prefix if transport is TCP (RFC 1035 4.2.2) */
    if (is_tcp && len >= 14 && ngx_dns_read_u16(buf) == (uint16_t)(len - 2)) {
        buf += 2;
        len -= 2;
    }

    /* Minimum DNS Header is 12 bytes */
    if (len < 12) {
        pkt->error_msg = "Packet too short (< 12 bytes)";
        return -1;
    }

    /* 1. Header Parsing */
    pkt->header.id      = ngx_dns_read_u16(buf);
    pkt->header.flags   = ngx_dns_read_u16(buf + 2);
    pkt->header.qdcount = ngx_dns_read_u16(buf + 4);
    pkt->header.ancount = ngx_dns_read_u16(buf + 6);
    pkt->header.nscount = ngx_dns_read_u16(buf + 8);
    pkt->header.arcount = ngx_dns_read_u16(buf + 10);

    /* Extract flag fields */
    uint16_t f = pkt->header.flags;
    pkt->header.qr     = (f >> 15) & 0x01;
    pkt->header.opcode = (f >> 11) & 0x0F;
    pkt->header.aa     = (f >> 10) & 0x01;
    pkt->header.tc     = (f >> 9)  & 0x01;
    pkt->header.rd     = (f >> 8)  & 0x01;
    pkt->header.ra     = (f >> 7)  & 0x01;
    pkt->header.z      = (f >> 4)  & 0x07;
    pkt->header.rcode  = f & 0x0F;

    size_t offset = 12;

    /* 2. Question Section Parsing */
    if (pkt->header.qdcount > 0) {
        size_t read_bytes = 0;
        if (ngx_dns_parse_name(buf, len, offset, pkt->question.qname,
                               sizeof(pkt->question.qname), &read_bytes) != 0) {
            pkt->error_msg = "Failed to parse question QNAME";
            return -1;
        }
        offset += read_bytes;

        if (offset + 4 > len) {
            pkt->error_msg = "Truncated question section";
            return -1;
        }

        pkt->question.qtype  = ngx_dns_read_u16(buf + offset);
        pkt->question.qclass = ngx_dns_read_u16(buf + offset + 2);
        offset += 4;

        pkt->question.qtype_str  = ngx_dns_type_to_str(pkt->question.qtype);
        pkt->question.qclass_str = ngx_dns_class_to_str(pkt->question.qclass);
    }

    /* Helper lambda/closure macro to parse RR arrays */
    #define PARSE_RR_SECTION(count, array, parsed_count)                      \
    do {                                                                      \
        for (uint16_t i = 0; i < (count) && (parsed_count) < NGX_DNS_MAX_RECORDS; i++) { \
            size_t name_read = 0;                                             \
            if (offset >= len) break;                                         \
            ngx_dns_rr_t *rr = &(array)[parsed_count];                        \
            if (ngx_dns_parse_name(buf, len, offset, rr->name,                \
                                   sizeof(rr->name), &name_read) != 0) {     \
                break;                                                        \
            }                                                                 \
            offset += name_read;                                              \
            if (offset + 10 > len) break;                                     \
            rr->type     = ngx_dns_read_u16(buf + offset);                    \
            rr->rr_class = ngx_dns_read_u16(buf + offset + 2);                \
            rr->ttl      = ngx_dns_read_u32(buf + offset + 4);                \
            rr->rdlength = ngx_dns_read_u16(buf + offset + 8);                \
            offset += 10;                                                     \
            rr->type_str = ngx_dns_type_to_str(rr->type);                     \
            if (offset + rr->rdlength > len) break;                           \
            ngx_dns_parse_rdata(buf, len, offset, rr->type, rr->rdlength, rr);\
            offset += rr->rdlength;                                           \
            (parsed_count)++;                                                 \
        }                                                                     \
    } while(0)

    /* 3. Answers */
    PARSE_RR_SECTION(pkt->header.ancount, pkt->answers, pkt->parsed_answers);

    /* 4. Authorities */
    PARSE_RR_SECTION(pkt->header.nscount, pkt->authorities, pkt->parsed_authorities);

    /* 5. Additionals */
    PARSE_RR_SECTION(pkt->header.arcount, pkt->additionals, pkt->parsed_additionals);

    #undef PARSE_RR_SECTION

    pkt->parsed_ok = 1;
    return 0;
}

size_t ngx_dns_packet_to_json(const ngx_dns_packet_t *pkt, char *json_buf, size_t max_len) {
    if (!pkt || !json_buf || max_len == 0) return 0;

    size_t len = 0;
    int n = snprintf(json_buf, max_len, "[");
    if (n > 0) {
        len += ((size_t)n < max_len) ? (size_t)n : max_len - 1;
    }

    for (uint16_t i = 0; i < pkt->parsed_answers; i++) {
        const ngx_dns_rr_t *rr = &pkt->answers[i];
        if (i > 0 && len < max_len - 1) {
            n = snprintf(json_buf + len, max_len - len, ",");
            if (n > 0) {
                len += ((size_t)n < max_len - len) ? (size_t)n : max_len - 1 - len;
            }
        }
        if (len < max_len - 1) {
            n = snprintf(json_buf + len, max_len - len,
                         "{\"name\":\"%.255s\",\"type\":\"%.31s\",\"ttl\":%u,\"data\":\"%.511s\"}",
                         rr->name, rr->type_str, rr->ttl,
                         rr->rdata_str[0] ? rr->rdata_str : rr->ip_str);
            if (n > 0) {
                len += ((size_t)n < max_len - len) ? (size_t)n : max_len - 1 - len;
            }
        }
    }

    if (len < max_len - 1) {
        n = snprintf(json_buf + len, max_len - len, "]");
        if (n > 0) {
            len += ((size_t)n < max_len - len) ? (size_t)n : max_len - 1 - len;
        }
    }

    if (len >= max_len) {
        len = max_len - 1;
    }
    json_buf[len] = '\0';
    return len;
}

size_t ngx_dns_answers_to_ips(const ngx_dns_packet_t *pkt, char *ip_buf, size_t max_len) {
    if (!pkt || !ip_buf || max_len == 0) return 0;

    size_t len = 0;
    ip_buf[0] = '\0';
    int count = 0;

    for (uint16_t i = 0; i < pkt->parsed_answers; i++) {
        const ngx_dns_rr_t *rr = &pkt->answers[i];
        if (rr->ip_str[0] != '\0') {
            if (count > 0 && len < max_len - 1) {
                int n = snprintf(ip_buf + len, max_len - len, ", ");
                if (n > 0) {
                    len += ((size_t)n < max_len - len) ? (size_t)n : max_len - 1 - len;
                }
            }
            if (len < max_len - 1) {
                int n = snprintf(ip_buf + len, max_len - len, "%.63s", rr->ip_str);
                if (n > 0) {
                    len += ((size_t)n < max_len - len) ? (size_t)n : max_len - 1 - len;
                }
                count++;
            }
        }
    }

    if (len >= max_len) {
        len = max_len - 1;
    }
    ip_buf[len] = '\0';
    return len;
}
