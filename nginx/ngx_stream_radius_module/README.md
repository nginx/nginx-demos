# ngx_stream_radius_module

A native C NGINX module that parses **RADIUS** authentication and accounting packets in the `stream` context, populating NGINX variables with all AVP (Attribute-Value Pair) fields defined in RFC 2865 and RFC 2866, including full **Vendor-Specific Attribute (VSA)** support with loadable vendor dictionaries.

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
5. [Directives Reference](#directives-reference)
6. [Variables Reference](#variables-reference)
   - [Packet-Level Variables](#packet-level-variables)
   - [Standard Attribute Variables](#standard-attribute-variables)
   - [Response Packet Variables](#response-packet-variables)
   - [Vendor-Specific Attribute Variables](#vendor-specific-attribute-variables)
   - [Dynamic Variable Lookup](#dynamic-variable-lookup)
7. [Dictionary File Format](#dictionary-file-format)
8. [Configuration Examples](#configuration-examples)
9. [Testing](#testing)
10. [Security Considerations](#security-considerations)
11. [References](#references)
12. [Contributing](#contributing)
13. [License](#license)

---

## Features

- **Full RFC 2865 parser** — Access-Request, Access-Accept, Access-Reject, Access-Challenge, Status-Server, Status-Client
- **Full RFC 2866 parser** — Accounting-Request, Accounting-Response
- **RFC 3576 / RFC 5176** packet codes — CoA-Request/ACK/NAK, Disconnect-Request/ACK/NAK
- **Vendor-Specific Attributes (VSA, type 26)** fully decoded per RFC 2865 §5.26
- **FreeRADIUS-compatible dictionary file** loader (`VENDOR`, `ATTRIBUTE`, `VALUE`, `BEGIN-VENDOR`, `END-VENDOR`, `$INCLUDE`)
- **All standard attribute data types**: `string`, `integer`, `ipaddr`, `ipv6addr`, `ipv6prefix`, `date`, `octets`, `ether`, `ifid`
- **NGINX variable population** for every parsed AVP — accessible from `log_format`, `map`, `proxy_pass`, `if`, etc.
- **Dynamic variable lookup** — `$radius_<name>` and `$radius_vsa_<Vendor>_<Attr>` resolved at runtime
- **Per-server configuration** — each `server {}` block can have independent settings and dictionaries
- Works over **UDP** (standard RADIUS transport)
- **Zero-copy** for the raw packet buffer; values decoded into the connection pool
- Compatible with both **Open Source NGINX** and **NGINX Plus** without source modifications

---

## Architecture

```
Client (NAS)
    │  UDP:1812 / UDP:1813
    ▼
┌─────────────────────────────────────────────────┐
│  NGINX stream server                            │
│                                                 │
│  NGX_STREAM_PREREAD_PHASE                       │
│    └─ ngx_stream_radius_preread_handler()       │
│         ├─ Buffer UDP datagram                  │
│         ├─ Parse RADIUS header (20 bytes)       │
│         ├─ Walk AVP list                        │
│         │    ├─ Standard attrs → decode value   │
│         │    └─ VSA (type 26) → decode sub-AVPs │
│         └─ Populate per-session context         │
│                                                 │
│  NGINX variables (available everywhere)         │
│    $radius_code, $radius_User_Name, ...    │
│    $radius_vsa_Cisco_Cisco-AVPair, ...          │
│                                                 │
│  proxy_pass → upstream RADIUS server            │
└─────────────────────────────────────────────────┘
```

The module operates in the **preread phase**, which means the full packet is parsed before any proxying begins. The raw bytes are forwarded unmodified to the upstream server — the module is **read-only** with respect to the packet content.

---

## Quick Start

```nginx
# nginx.conf
load_module modules/ngx_stream_radius_module.so;

stream {
    log_format radius '$remote_addr code=$radius_code_name '
                      'user="$radius_User_Name" '
                      'nas_ip=$radius_NAS_IP_Address';

    server {
        listen 1812 udp;

        radius_parse  on;
        radius_secret "shared-secret";
        radius_dict   /etc/nginx/radius/dictionary.cisco;

        access_log /var/log/nginx/radius.log radius;
        proxy_pass 10.0.0.1:1812;
    }
}
```

---

## Build Instructions

### Prerequisites

- NGINX source tree (same version as the installed binary for dynamic builds)
- GCC or Clang
- PCRE, OpenSSL, zlib development headers (standard NGINX dependencies)

### Dynamic Module (Recommended)

```bash
# 1. Download the NGINX source matching your installed version
nginx_version=$(nginx -v 2>&1 | grep -oP '[\d.]+' | head -n1)
wget https://nginx.org/download/nginx-${nginx_version}.tar.gz
tar xzf nginx-${nginx_version}.tar.gz

# 2. Clone this module
git clone https://github.com/nginx/nginx-demos.git

# 3. Configure with --add-dynamic-module
cd nginx-${nginx_version}
./configure \
    --with-compat \
    --with-stream \
    --with-stream_ssl_module \
    --add-dynamic-module=../nginx-demos/nginx/ngx_stream_radius_module

# 4. Build only the module (fast)
make modules

# 5. Install
sudo cp objs/ngx_stream_radius_module.so /etc/nginx/modules/

# 6. Add to nginx.conf (top level, before events {})
#    load_module modules/ngx_stream_radius_module.so;
```

### Static Module

```bash
nginx_version=$(nginx -v 2>&1 | grep -oP '[\d.]+' | head -n1)
cd nginx-${nginx_version}
./configure \
    --with-stream \
    --add-module=../ngx_stream_radius_module
make
sudo make install
```

### NGINX Plus

NGINX Plus supports dynamic modules built against the open-source compatibility layer:

```bash
# Obtain the NGINX Plus package version
nginx_version=$(nginx -v 2>&1 | grep -oP '[\d.]+' | head -n1)

# Download the corresponding OSS source
wget https://nginx.org/download/nginx-${nginx_version}.tar.gz
tar xzf nginx-${nginx_version}.tar.gz

cd nginx-${nginx_version}

# Use --with-compat to build against NGINX Plus ABI
./configure \
    --with-compat \
    --with-stream \
    --add-dynamic-module=../nginx-demos/nginx/ngx_stream_radius_module

make modules
sudo cp objs/ngx_stream_radius_module.so /etc/nginx/modules/
```

Add to `/etc/nginx/nginx.conf`:
```nginx
load_module modules/ngx_stream_radius_module.so;
```

Then reload: `nginx -s reload`

### Using the Convenience Makefile

```bash
# Dynamic build (default)
make NGINX_SRC=/path/to/nginx-source dynamic

# Static build
make NGINX_SRC=/path/to/nginx-source static
```

---

## Directives Reference

### `radius_parse`

**Syntax:** `radius_parse on | off;`  
**Default:** `off`  
**Context:** `stream`, `server`

Enables RADIUS packet pre-reading and parsing for the enclosing server block. When enabled, the module buffers the incoming UDP datagram in the preread phase, parses the RADIUS header and all AVPs, and populates NGINX variables.

```nginx
server {
    listen 1812 udp;
    radius_parse on;
}
```

---

### `radius_dict`

**Syntax:** `radius_dict /path/to/dictionary;`  
**Default:** —  
**Context:** `stream`, `server`

Loads a FreeRADIUS-compatible vendor dictionary file. May be specified multiple times to load multiple files. Dictionaries are loaded once at configuration time and shared across all workers.

```nginx
radius_dict /etc/nginx/radius/dictionary.cisco;
radius_dict /etc/nginx/radius/dictionary.mikrotik;
radius_dict /etc/nginx/radius/dictionary.juniper;
```

See [Dictionary File Format](#dictionary-file-format) for the file syntax.

---

### `radius_secret`

**Syntax:** `radius_secret <string>;`  
**Default:** `""`  
**Context:** `stream`, `server`

The RADIUS shared secret. Currently stored for use with Message-Authenticator verification (RFC 2869). When empty, cryptographic verification is skipped and all packets are accepted for parsing.

```nginx
radius_secret "V3ryS3cr3tP@ssw0rd";
```

> **Security note:** Store secrets in a separate file included with `include`, protected by filesystem permissions. See [Security Considerations](#security-considerations).

---

### `radius_buffer_size`

**Syntax:** `radius_buffer_size <size>;`  
**Default:** `4096`  
**Context:** `stream`, `server`

Maximum size in bytes of the RADIUS packet buffer. The RADIUS protocol limits packets to 4096 bytes (RFC 2865 §3), so the default is the RFC maximum. Reduce this only if your deployment uses a subset of attributes and you want tighter memory bounds per connection.

```nginx
radius_buffer_size 4096;
```

---

### `radius_var_prefix`

**Syntax:** `radius_var_prefix <prefix>;`  
**Default:** `radius_attr_`  
**Context:** `stream`, `server`

Sets the prefix used for standard attribute variables when performing dynamic name lookups. The built-in variable names (e.g., `$radius_User_Name`) always use `radius_attr_` regardless of this setting.

```nginx
radius_var_prefix my_radius_;
# Then use: $my_radius_User_Name
```

---

### `radius_vendor_var_prefix`

**Syntax:** `radius_vendor_var_prefix <prefix>;`  
**Default:** `radius_vsa_`  
**Context:** `stream`, `server`

Sets the prefix for vendor-specific attribute variables.

```nginx
radius_vendor_var_prefix vsa_;
# Then use: $vsa_Cisco_Cisco-AVPair
```

---

## Variables Reference

All variables are available in `stream` context directives that accept variables: `log_format`, `map`, `proxy_pass`, `set`, `return`, `access_log`, etc.

Variables return an **empty string** (not `not_found`) when the module is enabled but the attribute was not present in the packet. Variables are `not_found` when the module is disabled or the packet could not be parsed.

### Packet-Level Variables

| Variable | Description |
|---|---|
| `$radius_valid` | `1` if the packet was successfully parsed, `0` otherwise |
| `$radius_code` | Numeric packet code (1–255) |
| `$radius_code_name` | Human-readable packet code (e.g., `Access-Request`) |
| `$radius_identifier` | Packet identifier byte (0–255) |
| `$radius_length` | Declared packet length in bytes |
| `$radius_authenticator` | 16-byte authenticator as 32-character hex string |

### Standard Attribute Variables

Variables for all RFC 2865 and RFC 2866 standard attributes are pre-registered. Hyphens in attribute names are mapped to underscores in variable names.

#### RFC 2865 — Authentication Attributes

| Variable | Type | Attr # | Description |
|---|---|---|---|
| `$radius_User_Name` | string | 1 | User name being authenticated |
| `$radius_User_Password` | octets | 2 | Encrypted user password |
| `$radius_CHAP_Password` | octets | 3 | CHAP password |
| `$radius_NAS_IP_Address` | ipaddr | 4 | NAS IP address |
| `$radius_NAS_Port` | integer | 5 | NAS physical port |
| `$radius_Service_Type` | integer | 6 | Requested service type |
| `$radius_Framed_Protocol` | integer | 7 | Framing protocol (PPP, SLIP…) |
| `$radius_Framed_IP_Address` | ipaddr | 8 | Framed IP address |
| `$radius_Framed_IP_Netmask` | ipaddr | 9 | Framed IP netmask |
| `$radius_Framed_Routing` | integer | 10 | Routing method |
| `$radius_Filter_Id` | string | 11 | Name of filter list |
| `$radius_Framed_MTU` | integer | 12 | Maximum Transmission Unit |
| `$radius_Framed_Compression` | integer | 13 | Compression protocol |
| `$radius_Login_IP_Host` | ipaddr | 14 | Host to connect to |
| `$radius_Login_Service` | integer | 15 | Service to connect to |
| `$radius_Login_TCP_Port` | integer | 16 | TCP port to connect to |
| `$radius_Reply_Message` | string | 18 | Text to display to user |
| `$radius_Callback_Number` | string | 19 | Callback dialstring |
| `$radius_Callback_Id` | string | 20 | Name of callback place |
| `$radius_Framed_Route` | string | 22 | Routing info |
| `$radius_Framed_IPX_Network` | ipaddr | 23 | IPX network number |
| `$radius_State` | octets | 24 | State across Access-Challenge |
| `$radius_Class` | octets | 25 | Arbitrary value |
| `$radius_Vendor_Specific` | octets | 26 | Vendor-specific (raw, see VSA vars) |
| `$radius_Session_Timeout` | integer | 27 | Maximum session length (seconds) |
| `$radius_Idle_Timeout` | integer | 28 | Max consecutive idle seconds |
| `$radius_Termination_Action` | integer | 29 | Action on session termination |
| `$radius_Called_Station_Id` | string | 30 | Called station identifier |
| `$radius_Calling_Station_Id` | string | 31 | Calling station identifier |
| `$radius_NAS_Identifier` | string | 32 | NAS identifier string |
| `$radius_Proxy_State` | octets | 33 | Proxy chain state |
| `$radius_Login_LAT_Service` | string | 34 | LAT service |
| `$radius_Login_LAT_Node` | string | 35 | LAT node |
| `$radius_Login_LAT_Group` | octets | 36 | LAT group |
| `$radius_Framed_AppleTalk_Link` | integer | 37 | AppleTalk link number |
| `$radius_Framed_AppleTalk_Network` | integer | 38 | AppleTalk network |
| `$radius_Framed_AppleTalk_Zone` | string | 39 | AppleTalk zone |
| `$radius_CHAP_Challenge` | octets | 60 | CHAP challenge |
| `$radius_NAS_Port_Type` | integer | 61 | NAS port type |
| `$radius_Port_Limit` | integer | 62 | Max ports from NAS |
| `$radius_Login_LAT_Port` | string | 63 | LAT port |
| `$radius_Tunnel_Type` | integer | 64 | Tunnel type |
| `$radius_Tunnel_Medium_Type` | integer | 65 | Tunnel medium type |
| `$radius_Connect_Info` | string | 77 | Connect information |
| `$radius_Message_Authenticator` | octets | 80 | HMAC-MD5 (RFC 2869) |
| `$radius_NAS_Port_Id` | string | 87 | NAS port identifier string |
| `$radius_Framed_Pool` | octets | 88 | Pool from which framed address assigned |
| `$radius_NAS_IPv6_Address` | ipv6addr | 95 | NAS IPv6 address |

#### RFC 2866 — Accounting Attributes

| Variable | Type | Attr # | Description |
|---|---|---|---|
| `$radius_Acct_Status_Type` | integer | 40 | Start/Stop/Interim-Update |
| `$radius_Acct_Delay_Time` | integer | 41 | Seconds client tried to send packet |
| `$radius_Acct_Input_Octets` | integer | 42 | Octets received from port |
| `$radius_Acct_Output_Octets` | integer | 43 | Octets sent to port |
| `$radius_Acct_Session_Id` | string | 44 | Unique session identifier |
| `$radius_Acct_Authentic` | integer | 45 | How user was authenticated |
| `$radius_Acct_Session_Time` | integer | 46 | Session duration in seconds |
| `$radius_Acct_Input_Packets` | integer | 47 | Packets received from port |
| `$radius_Acct_Output_Packets` | integer | 48 | Packets sent to port |
| `$radius_Acct_Terminate_Cause` | integer | 49 | Cause of session termination |
| `$radius_Acct_Multi_Session_Id` | string | 50 | Multi-session unique identifier |
| `$radius_Acct_Link_Count` | integer | 51 | Links in a multi-link session |
| `$radius_Acct_Input_Gigawords` | integer | 52 | Gigaword wrap count for input |
| `$radius_Acct_Output_Gigawords` | integer | 53 | Gigaword wrap count for output |
| `$radius_Event_Timestamp` | date | 55 | Event timestamp (UNIX epoch) |

#### RFC 2869 — Extensions

| Variable | Attr | RFC attribute name | Decoded as |
|---|---|---|---|
| `$radius_event_timestamp` | 55 | Event-Timestamp | integer (Unix epoch) |
| `$radius_chap_challenge` | 60 | CHAP-Challenge | hex octets |
| `$radius_nas_port_type` | 61 | NAS-Port-Type | integer |
| `$radius_connect_info` | 77 | Connect-Info | string |
| `$radius_eap_message` | 79 | EAP-Message | hex octets |

#### RFC 3162 — IPv6

| Variable | Attr | RFC attribute name | Decoded as |
|---|---|---|---|
| `$radius_nas_ipv6_address` | 95 | NAS-IPv6-Address | colon-hex IPv6 |
| `$radius_framed_ipv6_prefix` | 97 | Framed-IPv6-Prefix | prefix/len |
| `$radius_framed_ipv6_pool` | 100 | Framed-IPv6-Pool | hex octets |

### Response Packet Variables

Populated from the upstream RADIUS server's response packet via a
transparent `send_chain` intercept.  The packet is forwarded to the
client unchanged.

> **Requires `proxy_responses 1`** on UDP servers — without it the
> session does not finalise until `proxy_timeout` and the log is
> written far too late.

#### Response header

| Variable | Description |
|---|---|
| `$radius_response_code` | Numeric code of the upstream response packet |
| `$radius_response_code_name` | Name of the upstream response (e.g. `Access-Accept`) |

#### Response attributes

| Variable | Attr | RFC attribute name | Decoded as |
|---|---|---|---|
| `$radius_resp_user_name` | 1 | User-Name | string |
| `$radius_resp_service_type` | 6 | Service-Type | integer |
| `$radius_resp_framed_protocol` | 7 | Framed-Protocol | integer |
| `$radius_resp_framed_ip_address` | 8 | Framed-IP-Address | dotted-decimal IPv4 |
| `$radius_resp_framed_ip_netmask` | 9 | Framed-IP-Netmask | dotted-decimal IPv4 |
| `$radius_resp_framed_routing` | 10 | Framed-Routing | integer |
| `$radius_resp_filter_id` | 11 | Filter-Id | string |
| `$radius_resp_framed_mtu` | 12 | Framed-MTU | integer |
| `$radius_resp_framed_compression` | 13 | Framed-Compression | integer |
| `$radius_resp_reply_message` | 18 | Reply-Message | string |
| `$radius_resp_state` | 24 | State | hex octets |
| `$radius_resp_class` | 25 | Class | hex octets |
| `$radius_resp_session_timeout` | 27 | Session-Timeout | integer (seconds) |
| `$radius_resp_idle_timeout` | 28 | Idle-Timeout | integer (seconds) |
| `$radius_resp_termination_action` | 29 | Termination-Action | integer |
| `$radius_resp_called_station_id` | 30 | Called-Station-Id | string |
| `$radius_resp_calling_station_id` | 31 | Calling-Station-Id | string |
| `$radius_resp_nas_identifier` | 32 | NAS-Identifier | string |
| `$radius_resp_tunnel_type` | 64 | Tunnel-Type | integer |
| `$radius_resp_tunnel_medium_type` | 65 | Tunnel-Medium-Type | integer |
| `$radius_resp_connect_info` | 77 | Connect-Info | string |
| `$radius_resp_eap_message` | 79 | EAP-Message | hex octets |
| `$radius_resp_msg_authenticator` | 80 | Message-Authenticator | hex octets |
| `$radius_resp_tunnel_private_group_id` | 81 | Tunnel-Private-Group-Id | string |
| `$radius_resp_acct_interim_interval` | 85 | Acct-Interim-Interval | integer |
| `$radius_resp_framed_pool` | 88 | Framed-Pool | hex octets |
| `$radius_resp_nas_ipv6_address` | 95 | NAS-IPv6-Address | colon-hex IPv6 |
| `$radius_resp_framed_ipv6_prefix` | 97 | Framed-IPv6-Prefix | prefix/len |
| `$radius_resp_framed_ipv6_pool` | 100 | Framed-IPv6-Pool | hex octets |

### Vendor-Specific Attribute Variables

After loading a vendor dictionary with `radius_dict`, VSA variables are available using the pattern:

```
$radius_vsa_<VendorName>_<AttributeName>
```

Hyphens in both vendor and attribute names are interchangeable with underscores.

**Examples** (with `dictionary.cisco` loaded):

```nginx
$radius_vsa_Cisco_Cisco-AVPair        # Cisco VSA type 1
$radius_vsa_Cisco_Cisco-Policy-Up     # Cisco VSA type 37
$radius_vsa_Cisco_Cisco-Policy-Down   # Cisco VSA type 38
$radius_vsa_Cisco_Cisco-Rate-Limit    # Cisco VSA type 8 (if defined)
```

**Examples** (with `dictionary.mikrotik` loaded):

```nginx
$radius_vsa_Mikrotik_Mikrotik-Rate-Limit   # MikroTik VSA type 8
$radius_vsa_Mikrotik_Mikrotik-Group        # MikroTik VSA type 3
$radius_vsa_Mikrotik_Mikrotik-Address-List # MikroTik VSA type 19
```

---

### Dynamic Variable Lookup

In addition to the pre-registered variables, you can reference **any** attribute by numeric code or dictionary name at configuration time:

```nginx
# By numeric type code
$radius_1    # same as $radius_User_Name
$radius_4    # same as $radius_NAS_IP_Address

# By dictionary name (hyphen/underscore interchangeable, case-insensitive)
$radius_User-Name       # same as $radius_User_Name
$radius_nas_ip_address  # same as $radius_NAS_IP_Address
```

---

## Dictionary File Format

The module uses a subset of the **FreeRADIUS dictionary format**. Existing FreeRADIUS dictionary files from `/usr/share/freeradius/` can be used directly.

### Supported Keywords

```
# Comment line

VENDOR      <VendorName>  <PEN>
BEGIN-VENDOR <VendorName>
ATTRIBUTE   <AttrName>   <TypeCode>   <DataType>  [<VendorName>]
VALUE       <AttrName>   <ValueName>  <Number>
END-VENDOR  <VendorName>
$INCLUDE    <filename>
```

### Data Type Keywords

| Keyword | Description |
|---|---|
| `string` | UTF-8 / ASCII string |
| `integer` | 32-bit unsigned integer |
| `ipaddr` | IPv4 address |
| `ipv6addr` | IPv6 address |
| `ipv6prefix` | IPv6 prefix |
| `date` | 32-bit UNIX timestamp |
| `octets` | Raw bytes (hex-encoded in variables) |
| `ether` | Ethernet MAC address |
| `ifid` | Interface identifier |
| `integer64` | 64-bit unsigned integer |
| `byte` | 8-bit integer (stored as integer) |
| `short` | 16-bit integer (stored as integer) |

### Example Custom Dictionary

```text
# dictionary.myvendor
# My Company IANA PEN: 99999

VENDOR      MyVendor        99999

BEGIN-VENDOR MyVendor

ATTRIBUTE   MyVendor-Username       1   string
ATTRIBUTE   MyVendor-Bandwidth-Up   2   integer
ATTRIBUTE   MyVendor-Bandwidth-Down 3   integer
ATTRIBUTE   MyVendor-VLAN-Id        4   integer
ATTRIBUTE   MyVendor-Policy-Name    5   string

VALUE       MyVendor-Bandwidth-Up   1Mbps    1000000
VALUE       MyVendor-Bandwidth-Up   10Mbps   10000000
VALUE       MyVendor-Bandwidth-Up   100Mbps  100000000

END-VENDOR  MyVendor
```

Load it with:
```nginx
radius_dict /etc/nginx/radius/dictionary.myvendor;
```

Access values:
```nginx
$radius_vsa_MyVendor_MyVendor-Username
$radius_vsa_MyVendor_MyVendor-Bandwidth-Up
$radius_vsa_MyVendor_MyVendor-VLAN-Id
```

---

## Configuration Examples

### Basic Auth Proxy with Logging

```nginx
stream {
    log_format radius_log
        '$time_iso8601 $remote_addr '
        'code=$radius_code_name '
        'user="$radius_User_Name" '
        'nas=$radius_NAS_IP_Address';

    server {
        listen 1812 udp;
        radius_parse  on;
        radius_secret "secret";
        access_log /var/log/nginx/radius.log radius_log;
        proxy_pass 10.0.0.1:1812;
    }
}
```

### Multi-Upstream Routing by NAS-Port-Type

```nginx
stream {
    map $radius_NAS_Port_Type $radius_upstream {
        default  "10.0.0.10:1812";  # Async / unknown
        15       "10.0.0.11:1812";  # Ethernet
        19       "10.0.0.12:1812";  # Wireless 802.11
        5        "10.0.0.13:1812";  # Virtual (VPN)
    }

    server {
        listen 1812 udp;
        radius_parse  on;
        radius_secret "secret";
        proxy_pass    $radius_upstream;
    }
}
```

### Route by Called-Station-Id (Wi-Fi SSID in MAC:SSID format)

```nginx
stream {
    map $radius_Called_Station_Id $radius_upstream {
        default                    "10.0.0.10:1812";
        "~*:corp-wifi$"            "10.0.0.11:1812";
        "~*:guest-wifi$"           "10.0.0.12:1812";
        "~*:iot-wifi$"             "10.0.0.13:1812";
    }

    server {
        listen 1812 udp;
        radius_parse  on;
        radius_secret "secret";
        proxy_pass    $radius_upstream;
    }
}
```

### Cisco VSA — Route by Policy

```nginx
stream {
    map $radius_vsa_Cisco_Cisco-AVPair $radius_upstream {
        default                    "10.0.0.10:1812";
        "~*shell:priv-lvl=15"      "10.0.0.11:1812";  # Admin users
    }

    server {
        listen 1812 udp;
        radius_parse  on;
        radius_dict   /etc/nginx/radius/dictionary.cisco;
        radius_secret "secret";
        proxy_pass    $radius_upstream;
    }
}
```

### MikroTik Hotspot with Rate Limiting Logging

```nginx
stream {
    log_format hotspot
        '$time_iso8601 '
        'user="$radius_User_Name" '
        'mac="$radius_Calling_Station_Id" '
        'rate="$radius_vsa_Mikrotik_Mikrotik-Rate-Limit" '
        'group="$radius_vsa_Mikrotik_Mikrotik-Group" '
        'session="$radius_Acct_Session_Id" '
        'bytes_in=$radius_Acct_Input_Octets '
        'bytes_out=$radius_Acct_Output_Octets '
        'duration=$radius_Acct_Session_Time';

    server {
        listen 1812 udp;
        radius_parse on;
        radius_dict  /etc/nginx/radius/dictionary.mikrotik;
        radius_secret "hotspot-secret";
        access_log /var/log/nginx/hotspot_auth.log hotspot;
        proxy_pass 10.0.0.1:1812;
    }

    server {
        listen 1813 udp;
        radius_parse on;
        radius_dict  /etc/nginx/radius/dictionary.mikrotik;
        radius_secret "hotspot-secret";
        access_log /var/log/nginx/hotspot_acct.log hotspot;
        proxy_pass 10.0.0.1:1813;
    }
}
```

### Access Restriction by NAS IP Range

```nginx
stream {
    geo $radius_allowed_nas {
        default         0;
        10.0.0.0/24     1;
        192.168.100.0/24 1;
    }

    server {
        listen 1812 udp;
        radius_parse on;
        radius_secret "secret";

        # Only proxy from trusted NAS devices
        # (combine with stream access module or custom logic)
        proxy_pass 10.0.0.1:1812;
    }
}
```

---

## Testing

### Unit Tests (no NGINX required)

```bash
cd tests
make
```

All test cases are self-contained and use a portable shim (`test_shim.h`) that re-implements the minimal NGINX types needed. No NGINX source or installation is required.

Test coverage includes:
- Access-Request header parsing
- Standard attribute decoding (string, integer, ipaddr)
- Accounting-Request attribute parsing
- VSA (Cisco) decoding
- Malformed / truncated packet rejection
- Dictionary file loading and lookup

### Integration Testing with `radtest`

After building and loading the module:

```bash
# Install FreeRADIUS client tools
apt-get install freeradius-utils   # Debian/Ubuntu
yum install freeradius-utils       # RHEL/CentOS

# Send a test Access-Request
radtest testuser testpass 127.0.0.1:1812 0 shared-secret

# Send an Accounting-Request (Start)
radclient 127.0.0.1:1813 acct shared-secret \
    << 'EOF'
Acct-Status-Type = Start
User-Name = "testuser"
Acct-Session-Id = "test-001"
NAS-IP-Address = 10.0.0.1
EOF
```

Verify NGINX logs contain correctly parsed attribute values.

### Packet Capture Verification

```bash
# Capture RADIUS traffic to verify module does not modify packets
tcpdump -i lo -w /tmp/radius.pcap udp port 1812
# ... send test packets ...
tshark -r /tmp/radius.pcap -d udp.port==1812,radius -V
```

---

## Security Considerations

1. **Shared Secret**: Store the RADIUS shared secret in a separate file with restrictive permissions:
   ```bash
   chmod 640 /etc/nginx/radius_secret.conf
   chown root:nginx /etc/nginx/radius_secret.conf
   ```
   ```nginx
   # nginx.conf
   include /etc/nginx/radius_secret.conf;
   ```
   ```nginx
   # radius_secret.conf
   radius_secret "V3ryS3cr3tP@ssw0rd";
   ```

2. **UDP Source Validation**: RADIUS uses UDP, which is trivially spoofable. Use firewall rules to restrict which source IPs can reach the NGINX RADIUS listener:
   ```bash
   iptables -A INPUT -p udp --dport 1812 -s 10.0.0.0/24 -j ACCEPT
   iptables -A INPUT -p udp --dport 1812 -j DROP
   ```

3. **Message-Authenticator**: For Access-Requests from EAP-capable supplicants, `Message-Authenticator` (attr 80) is mandatory per RFC 5080. The module parses this attribute and stores it in `$radius_Message_Authenticator`. Future versions will include full HMAC-MD5 verification when `radius_secret` is set.

4. **Dictionary Files**: Dictionary files are loaded as root during `nginx -t` / startup. Ensure they are not world-writable:
   ```bash
   chmod 644 /etc/nginx/radius/*.dict
   chown root:root /etc/nginx/radius/*.dict
   ```

5. **Buffer Size**: The default 4096-byte buffer matches the RFC maximum. Do not increase it; the RADIUS protocol guarantees packets will not exceed this size.

6. **Sensitive Attributes**: `$radius_User_Password` (attr 2) is stored as hex-encoded encrypted bytes — it is **not** the cleartext password. Never log it in production unless required for debugging.

---

## References

- [RFC 2865](https://www.rfc-editor.org/rfc/rfc2865) — RADIUS
- [RFC 2866](https://www.rfc-editor.org/rfc/rfc2866) — RADIUS Accounting
- [RFC 2868](https://www.rfc-editor.org/rfc/rfc2868) — RADIUS Tunnel Attributes
- [RFC 2869](https://www.rfc-editor.org/rfc/rfc2869) — RADIUS Extensions
- [RFC 3162](https://www.rfc-editor.org/rfc/rfc3162) — RADIUS and IPv6
- [RFC 6613](https://www.rfc-editor.org/rfc/rfc6613) — RADIUS over TCP
- [NGINX Stream Module](https://nginx.org/en/docs/stream/ngx_stream_core_module.html)

---

## Contributing

See [CONTRIBUTING](CONTRIBUTING.md)

---

## License

Apache 2.0 — see [LICENSE](LICENSE).
