# ngx_stream_dns_module

A native C NGINX module that provides a full **DNS protocol parser** in the `stream` subsystem for both **UDP** and **TCP** DNS traffic. It decodes request and response wire packets and populates NGINX variables with DNS query names, record types, response status codes, answer IP addresses, and complete JSON representations.

Compatible with **NGINX Open Source ≥ 1.11.5** and **NGINX Plus ≥ R11** (both static and dynamic module builds).

---

## Table of Contents

1. [Features](#features)
2. [Architecture](#architecture)
3. [Quick Start](#quick-start)
4. [Build Instructions](#build-instructions)
   - [Dynamic Module (Recommended)](#dynamic-module-recommended)
   - [Static Module](#static-module)
   - [NGINX Plus](#nginx-plus)
   - [Using the Convenience Makefile](#using-the-convenience-makefile)
5. [Directives Reference](#directives-reference)
6. [Variables Reference](#variables-reference)
   - [DNS Request Variables](#dns-request-variables)
   - [DNS Response Variables](#dns-response-variables)
7. [Configuration Examples](#configuration-examples)
   - [1. UDP & TCP DNS Proxy with Access Logging](#1-udp--tcp-dns-proxy-with-access-logging)
   - [2. Dynamic Upstream Query Routing](#2-dynamic-upstream-query-routing)
8. [Testing](#testing)
9. [Security Considerations](#security-considerations)
10. [License](#license)

---

## Features

- **Full DNS Wire Protocol Parser** — RFC 1035, RFC 3596 (IPv6 AAAA), RFC 6891 (EDNS0), RFC 2782 (SRV), SOA, MX, CNAME, TXT, PTR, NS
- **Dual Transport Support** — Parses both UDP datagrams and TCP streams (handling 2-byte framing prefixes automatically)
- **Live Stream Interception** — Client requests parsed in `NGX_STREAM_PREREAD_PHASE`; upstream responses captured live via filter chain (`ngx_stream_top_filter`)
- **Zero Memory Allocation Decoding** — Reads directly from connection and stream buffers with zero heap allocations during parsing
- **Comprehensive NGINX Variables** — Exposes `$dns_request_name`, `$dns_request_type`, `$dns_response_rcode_str`, `$dns_response_ips`, `$dns_response_answers_json`, etc.
- **Per-Server Configuration** — Independent `dns_parse` and size limit settings per `server {}` block
- **Compatible with Open Source NGINX and NGINX Plus** without source modifications

---

## Architecture

```
Client (dig / resolver)
    │  UDP:53 / TCP:53
    ▼
┌────────────────────────────────────────────────────────┐
│  NGINX stream server                                   │
│                                                        │
│  NGX_STREAM_PREREAD_PHASE                              │
│    └─ ngx_stream_dns_preread_handler()                 │
│         ├─ Intercept UDP/TCP client request datagram   │
│         ├─ Parse DNS Header & Question Section         │
│         └─ Populate $dns_request_* variables           │
│                                                        │
│  STREAM FILTER CHAIN                                   │
│    └─ ngx_stream_dns_filter()                          │
│         ├─ Capture upstream DNS response datagram      │
│         ├─ Parse Answer RDATA (A, AAAA, MX, JSON...)   │
│         └─ Populate $dns_response_* variables          │
│                                                        │
│  access_log / proxy_pass → upstream DNS server         │
└────────────────────────────────────────────────────────┘
```

The module operates in the **preread phase** for incoming requests and hooks into the **stream filter chain** for upstream responses. Wire bytes are forwarded unmodified to upstream servers — the module is **read-only** with respect to DNS packet content.

---

## Quick Start

```nginx
# nginx.conf
load_module modules/ngx_stream_dns_module.so;

stream {
    log_format dns_log escape=none '$remote_addr [$time_local] $protocol '
                      'qname="$dns_request_name" qtype="$dns_request_type" class="$dns_request_class" id="$dns_request_id" '
                      'rcode="$dns_response_rcode_str" answers="$dns_response_ancount" ips="$dns_response_ips" json="$dns_response_answers_json"';

    upstream dns_servers {
        server 192.168.2.13:53;
    }

    server {
        listen 53 udp;
        listen 53;

        dns_parse on;
        proxy_responses 1;

        access_log /var/log/nginx/dns_access.log dns_log;

        proxy_pass dns_servers;
    }
}
```

---

## Build Instructions

### Prerequisites

- NGINX source tree (matching installed NGINX version for dynamic module builds)
- GCC or Clang
- PCRE, OpenSSL, zlib development headers

### Dynamic Module (Recommended)

```bash
# 1. Download the NGINX source matching your installed version
nginx_version=$(nginx -v 2>&1 | grep -oP '[\d.]+' | head -n1)
wget https://nginx.org/download/nginx-${nginx_version}.tar.gz
tar xzf nginx-${nginx_version}.tar.gz

# 2. Configure with --add-dynamic-module
cd nginx-${nginx_version}
./configure \
    --with-compat \
    --with-stream \
    --add-dynamic-module=/path/to/ngx_stream_dns_module

# 3. Build module
make modules

# 4. Install
sudo cp objs/ngx_stream_dns_module.so /etc/nginx/modules/

# 5. Add to top level of nginx.conf
#    load_module modules/ngx_stream_dns_module.so;
```

### Static Module

```bash
nginx_version=$(nginx -v 2>&1 | grep -oP '[\d.]+' | head -n1)
cd nginx-${nginx_version}
./configure \
    --with-stream \
    --add-module=/path/to/ngx_stream_dns_module
make
sudo make install
```

### NGINX Plus

```bash
nginx_version=$(nginx -v 2>&1 | grep -oP '[\d.]+' | head -n1)
wget https://nginx.org/download/nginx-${nginx_version}.tar.gz
tar xzf nginx-${nginx_version}.tar.gz

cd nginx-${nginx_version}
./configure \
    --with-compat \
    --with-stream \
    --add-dynamic-module=/path/to/ngx_stream_dns_module

make modules
sudo cp objs/ngx_stream_dns_module.so /etc/nginx/modules/
```

Add to `/etc/nginx/nginx.conf`:
```nginx
load_module modules/ngx_stream_dns_module.so;
```

Then reload: `nginx -s reload`

### Using the Convenience Makefile

```bash
# Dynamic build
make NGINX_SRC=/path/to/nginx-source dynamic

# Static build
make NGINX_SRC=/path/to/nginx-source static
```

---

## Directives Reference

### `dns_parse`

**Syntax:** `dns_parse on | off;`  
**Default:** `off`  
**Context:** `stream`, `server`

Enables or disables DNS packet pre-reading and response filter parsing for the enclosing server block. When enabled, the module intercepts incoming client requests and upstream responses, populating all `$dns_request_*` and `$dns_response_*` variables.

```nginx
server {
    listen 53 udp;
    dns_parse on;
}
```

---

### `dns_parse_max_size`

**Syntax:** `dns_parse_max_size <size>;`  
**Default:** `4096`  
**Context:** `stream`, `server`

Sets the maximum size in bytes of the DNS wire payload buffer to parse.

```nginx
dns_parse_max_size 8192;
```

---

## Variables Reference

### DNS Request Variables

| Variable | Description | Example Output |
| :--- | :--- | :--- |
| `$dns_request_name` / `$dns_request_qname` | Requested domain name | `google.com` |
| `$dns_request_type` / `$dns_request_qtype` | Human-readable query type | `A`, `AAAA`, `MX`, `TXT`, `SRV` |
| `$dns_request_class` / `$dns_request_qclass` | Human-readable query class | `IN`, `CH` |
| `$dns_request_id` | DNS transaction ID | `22910` |

### DNS Response Variables

| Variable | Description | Example Output |
| :--- | :--- | :--- |
| `$dns_response_id` | DNS transaction ID | `22910` |
| `$dns_response_rcode` | Numeric response code | `0` (NOERROR), `1` (FORMERR), `2` (SERVFAIL), `3` (NXDOMAIN), `4` (NOTIMP), `5` (REFUSED) |
| `$dns_response_rcode_str` | Human-readable response code | `NOERROR`, `FORMERR`, `SERVFAIL`, `NXDOMAIN`, `NOTIMP`, `REFUSED` |
| `$dns_response_ancount` | Number of answer records parsed | `6` |
| `$dns_response_ips` | Comma-separated list of IPv4/IPv6 answer addresses | `209.85.203.113, 209.85.203.102` |
| `$dns_response_answers_json` | Complete JSON array of parsed answer records | `[{"name":"google.com","type":"A","ttl":208,"data":"209.85.203.113"}]` |

---

## Configuration Examples

### 1. UDP & TCP DNS Proxy with Access Logging

```nginx
load_module modules/ngx_stream_dns_module.so;

stream {
    log_format dns_log escape=none '$remote_addr [$time_local] $protocol '
                      'qname="$dns_request_name" qtype="$dns_request_type" class="$dns_request_class" id="$dns_request_id" '
                      'rcode="$dns_response_rcode_str" answers="$dns_response_ancount" ips="$dns_response_ips" json="$dns_response_answers_json"';

    upstream dns_backend {
        server 10.0.0.1:53;
        server 10.0.0.2:53;
    }

    server {
        listen 53 udp;
        listen 53;

        dns_parse on;
        proxy_responses 1;

        access_log /var/log/nginx/dns_access.log dns_log;
        proxy_pass dns_backend;
    }
}
```

### 2. Dynamic Upstream Query Routing

```nginx
stream {
    dns_parse on;

    map $dns_request_name $dns_backend {
        .internal.company.com  internal_dns;
        default                public_dns;
    }

    upstream internal_dns {
        server 192.168.1.1:53;
    }

    upstream public_dns {
        server 1.1.1.1:53;
        server 8.8.8.8:53;
    }

    server {
        listen 53 udp;
        listen 53;

        proxy_responses 1;
        proxy_pass $dns_backend;
    }
}
```

---

## Testing

Run unit tests locally using standard C unit test framework:

```bash
make test
```

Or build and run the Docker test suite:

```bash
docker build -t ngx_stream_dns_module .
docker run --rm -p 20053:5353/udp -p 20053:5353 ngx_stream_dns_module
```

Query with `dig`:

```bash
dig @127.0.0.1 -p 20053 -t a google.com
dig @127.0.0.1 -p 20053 -t a google.com +tcp
```

---

## Security Considerations

- **Buffer Limits**: Set `dns_parse_max_size` appropriately (default `4096`) to prevent memory exhaustion from oversized UDP datagrams.
- **UDP Session Finalization**: Always set `proxy_responses 1;` when proxying UDP DNS to finalize sessions immediately after receiving 1 response and write access logs without delay.

---

## License

Licensed under the Apache License, Version 2.0
