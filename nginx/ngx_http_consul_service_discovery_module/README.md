# ngx_http_consul_service_discovery_module

A native C NGINX module that exposes selected `server {}` blocks as a JSON
service catalogue via a configurable location endpoint.  It does **not** talk
to Consul directly; a sidecar, cron job, or Consul agent can poll the endpoint
and register/deregister services through the
[Consul HTTP API](https://developer.hashicorp.com/consul/api-docs/agent/service).

**Port and address are discovered automatically** from the `listen` and
`server_name` directives — no manual override directives are required for
standard deployments.

Compatible with **NGINX Open Source ≥ 1.13** (static and dynamic module builds).

---

## Table of Contents

1. [Features](#features)
2. [How Auto-Discovery Works](#how-auto-discovery-works)
3. [Architecture](#architecture)
4. [Quick Start](#quick-start)
5. [Build Instructions](#build-instructions)
   - [Static Module](#static-module)
   - [Dynamic Module](#dynamic-module)
6. [Directives Reference](#directives-reference)
7. [JSON Response Format](#json-response-format)
8. [Configuration Examples](#configuration-examples)
9. [Testing](#testing)
10. [Security Considerations](#security-considerations)
11. [Known Limitations](#known-limitations)
12. [References](#references)
13. [License](#license)

---

## Features

- **Automatic port discovery** — the module walks `cmcf->ports` at
  postconfiguration time to read each server's listen port directly from the
  parsed `listen` directive; no `consul_service_port_override` required
- **Automatic address discovery** — the `address` field in the JSON is
  populated from the primary `server_name` value (FQDN or IP literal)
- **NAT / load-balancer friendly** — `consul_service_port_override` is
  available to advertise an external port that differs from the NGINX listen
  port, while the address is still auto-discovered
- **Zero Consul dependency** — expose service metadata from NGINX config alone
- **Standard NGINX build system** — static (`--add-module`) and dynamic
  (`--add-dynamic-module`) builds supported
- **JSON catalogue endpoint** — any location can serve
  `GET /consul/services`; restrict access with `allow`/`deny`
- **Static snapshot** — catalogue is built once at startup in the
  postconfiguration phase; request handling is a plain array walk with no
  parsing overhead

---

## How Auto-Discovery Works

### Port

During the NGINX configuration parse, every `listen` directive causes the core
module to call `ngx_http_add_listen()`, which inserts an entry into
`cmcf->ports` — a flat array of `ngx_http_conf_port_t` structs (one per unique
port number).  Each port entry holds an `addrs` array; each address holds a
`servers` array of `ngx_http_core_srv_conf_t *` pointers for every virtual
server listening on that address:port.

```
cmcf->ports[]                    (ngx_http_conf_port_t)
  .port  ← host-byte-order port  (ngx_inet_get_port applies ntohs)
  .addrs[]                       (ngx_http_conf_addr_t)
    .opt.addr_text  ← "IP:port"
    .servers[]      ← ngx_http_core_srv_conf_t *  ← matched by pointer
```

NGINX calls all `postconfiguration` hooks (line 309 in `ngx_http.c`) **before**
it calls `ngx_http_optimize_servers()` (line 335), which means `cmcf->ports`
and its nested arrays are still fully populated on `cf->temp_pool` when our
hook runs.

The module's `ngx_http_consul_find_listen_port()` iterates
`ports → addrs → servers[]` and compares each `ngx_http_core_srv_conf_t *`
against the target by pointer equality.  When a match is found it returns
`ports[p].port` — already in host byte order.

### Address

The `address` field is the bind IP from the `listen` directive, obtained by
calling `ngx_sock_ntop(opt.sockaddr, opt.socklen, buf, len, 0)` on the matched
`ngx_http_conf_addr_t`.  The `port=0` argument produces bare IP text with no
port suffix (`opt.addr_text` includes the port — this call does not).

| `listen` directive | `address` in JSON |
|--------------------|-------------------|
| `listen 80;` | `"0.0.0.0"` |
| `listen 0.0.0.0:80;` | `"0.0.0.0"` |
| `listen 1.2.3.4:80;` | `"1.2.3.4"` |
| `listen [::]:80;` | `"::"` |
| `listen [::1]:443;` | `"::1"` |
| `listen 127.0.0.1:8080;` | `"127.0.0.1"` |

The text buffer (`NGX_SOCKADDR_STRLEN` bytes) is allocated on `cf->pool`
(permanent pool) and remains valid for the worker lifetime.

---

## Architecture

```
NGINX config parse
  │  every listen directive → ngx_http_add_listen()
  │  every server_name     → cscf->server_name
  ▼
postconfiguration hook  (before ngx_http_optimize_servers)
  │  ngx_http_consul_find_listen_port():
  │    walk cmcf->ports → addrs → servers[]
  │    match cscf by pointer → read port (host byte order)
  │  ngx_sock_ntop(opt.sockaddr, port=0) → IP-only address text
  ▼
in-memory service catalogue  (ngx_array_t in main conf, permanent pool)
  │
  ▼  GET /consul/services
JSON response ──► Consul agent / sidecar / external poller
                        │
                        ▼
                  PUT /v1/agent/service/register
```

---

## Quick Start

```nginx
http {

    # ── Discovery endpoint ───────────────────────────────────────────────
    server {
        listen 8080;
        server_name discovery.internal;

        location /consul/services {
            consul_service_discovery on;
        }
    }

    # ── A discoverable service ───────────────────────────────────────────
    # Port (443) and address (api.example.com) are discovered automatically.
    server {
        listen 443 ssl;
        server_name api.example.com;

        consul_service_discoverable on;
        consul_service_name         "my-api";
        consul_service_tags         "v2,production,https";

        ssl_certificate     /etc/ssl/cert.pem;
        ssl_certificate_key /etc/ssl/key.pem;

        location / { proxy_pass http://backend; }
    }
}
```

```bash
curl -s http://discovery.internal:8080/consul/services | jq .
```

```json
{
  "services": [
    {
      "name": "my-api",
      "address": "0.0.0.0",
      "port": 443,
      "tags": "v2,production,https"
    }
  ]
}
```

---

## Build Instructions

### Prerequisites

- NGINX source tree (same version as the installed binary for dynamic builds)
- GCC or Clang
- PCRE, OpenSSL, zlib development headers (standard NGINX dependencies)


### Dynamic Module (Recommended)

#### 1. Download the NGINX source matching your installed version
```bash
nginx_version=$(nginx -v 2>&1 | grep -oP '[\d.]+' | head -n1)
wget https://nginx.org/download/nginx-${nginx_version}.tar.gz
tar xzf nginx-${nginx_version}.tar.gz
```

#### 2. Clone this module
```bash
git clone https://github.com/nginx/nginx-demos.git
```

#### 3. Configure with --add-dynamic-module
```bash
cd nginx-${nginx_version}
./configure \
    --with-compat          \
    --with-http_ssl_module \
    --with-http_v2_module  \
    --add-dynamic-module=../nginx-demos/nginx/ngx_http_consul_service_discovery_module
```

#### 4. Build only the module (fast)
```bash
make modules -j$(nproc)
```

#### 5. Install
```bash
sudo cp objs/ngx_http_consul_service_discovery_module.so /etc/nginx/modules/
```

#### 6. Add to nginx.conf (top level, before events {})
```bash
load_module modules/ngx_http_consul_service_discovery_module.so;
```

### Static Module

```bash
nginx_version=$(nginx -v 2>&1 | grep -oP '[\d.]+' | head -n1)
cd nginx-${nginx_version}
./configure \
    --with-compat          \
    --with-http_ssl_module \
    --with-http_v2_module  \
    --add-module=../nginx-demos/nginx/ngx_http_consul_service_discovery_module
make -j$(nproc)
sudo make install
```

### Makefile targets

```bash
make static  NGINX_SRC=/path/to/nginx   # build static
make dynamic NGINX_SRC=/path/to/nginx   # build dynamic .so (default)
make test    NGINX_SRC=/path/to/nginx   # build + run smoke tests
make clean   NGINX_SRC=/path/to/nginx   # remove build artefacts
```

### Docker

```bash
docker build -t ngx-consul-discovery .
docker run --rm -p 8080:8080 ngx-consul-discovery
curl http://localhost:8080/consul/services
```

---

## Directives Reference

### `consul_service_discovery`

**Syntax:** `consul_service_discovery on | off;`  
**Default:** `off`  
**Context:** `location`

Enables the JSON catalogue endpoint for this location.  `GET` returns the full
service list as JSON; `HEAD` returns only headers.

```nginx
location /consul/services {
    consul_service_discovery on;
}
```

---

### `consul_service_discoverable`

**Syntax:** `consul_service_discoverable on | off;`  
**Default:** `off`  
**Context:** `server`

Marks this virtual server as a discoverable service.  Only server blocks with
this directive set to `on` appear in the JSON catalogue.

---

### `consul_service_name`

**Syntax:** `consul_service_name "<name>";`  
**Default:** first `server_name` value  
**Context:** `server`

Logical service name in the JSON `name` field.  When omitted the first
`server_name` value is used.

---

### `consul_service_tags`

**Syntax:** `consul_service_tags "<tag1,tag2,...>";`  
**Default:** `"nginx"`  
**Context:** `server`

Comma-separated tag string in the JSON `tags` field.  Tag semantics are
defined by the consumer; the module treats them as opaque.

---

### `consul_service_port_override`

**Syntax:** `consul_service_port_override <port>;`  
**Default:** *(port auto-discovered from* `listen` *directive)*  
**Context:** `server`

**Optional.** Overrides the auto-discovered listen port in the JSON catalogue.
Use this only when the external port consumers should connect to differs from
the NGINX listen port — for example, behind NAT or a public load balancer.

```nginx
# NGINX listens on 8443 internally; public LB exposes :443
server {
    listen 8443 ssl;
    server_name api.example.com;          # address auto-discovered

    consul_service_discoverable  on;
    consul_service_name          "api";
    consul_service_port_override 443;     # override only the port
}
```

When not set, the port is read directly from the `listen` directive.

---

## JSON Response Format

`GET /consul/services` → `200 OK`, `Content-Type: application/json`:

```json
{
  "services": [
    {
      "name":    "api1",
      "address": "api1.example.com",
      "port":    80,
      "tags":    "v1,public"
    },
    {
      "name":    "api2",
      "address": "api2.example.com",
      "port":    443,
      "tags":    "v2,https"
    }
  ]
}
```

| Field | Source | Notes |
|-------|--------|-------|
| `name` | `consul_service_name` or first `server_name` | |
| `address` | bind IP from the `listen` directive | `"0.0.0.0"` for wildcard; specific IP for bound addresses |
| `port` | `listen` directive, or `consul_service_port_override` | host byte order |
| `tags` | `consul_service_tags` or `"nginx"` | comma-separated string |

---

## Configuration Examples

### Standard setup — no overrides needed

Port and address are fully automatic:

```nginx
http {
    server {
        listen 8080;
        server_name discovery.internal;

        location /consul/services {
            consul_service_discovery on;
            allow 10.0.0.0/8;
            deny  all;
        }
    }

    server {
        listen 80;
        server_name api1.example.com;

        consul_service_discoverable on;
        consul_service_name         "api1";
        consul_service_tags         "v1,http";

        location / { proxy_pass http://api1-backend; }
    }

    server {
        listen 443 ssl;
        server_name api2.example.com;

        consul_service_discoverable on;
        consul_service_name         "api2";
        consul_service_tags         "v1,https";

        ssl_certificate     /etc/ssl/api2.crt;
        ssl_certificate_key /etc/ssl/api2.key;

        location / { proxy_pass http://api2-backend; }
    }
}
```

Result:
```json
{"services":[
  {"name":"api1","address":"0.0.0.0","port":80,"tags":"v1,http"},
  {"name":"api2","address":"0.0.0.0","port":443,"tags":"v1,https"}
]}
```

### NAT / load-balancer port override

Only the port needs overriding; the address is still auto-discovered:

```nginx
server {
    listen      8080;
    server_name internal-svc.corp;

    consul_service_discoverable  on;
    consul_service_name          "internal-svc";
    consul_service_tags          "v2,internal";
    consul_service_port_override 80;   # LB maps :80 → :8080 internally
}
```

Result:
```json
{"services":[
  {"name":"internal-svc","address":"0.0.0.0","port":80,"tags":"v2,internal"}
]}
```

### Consul agent sidecar registration script

```bash
#!/usr/bin/env bash
# register-services.sh — poll the NGINX catalogue and register in Consul

DISCOVERY_URL="http://127.0.0.1:8080/consul/services"
CONSUL_URL="http://127.0.0.1:8500/v1/agent/service/register"

curl -sf "$DISCOVERY_URL" | jq -c '.services[]' | while read -r svc; do
    name=$(echo "$svc" | jq -r '.name')
    addr=$(echo "$svc" | jq -r '.address')
    port=$(echo "$svc" | jq -r '.port')
    tags=$(echo "$svc" | jq -r '.tags | split(",") | map(ltrimstr(" "))')

    curl -sf -X PUT "$CONSUL_URL" \
        -H "Content-Type: application/json" \
        -d "{
              \"Name\":    \"$name\",
              \"Address\": \"$addr\",
              \"Port\":    $port,
              \"Tags\":    $tags
            }"

    echo "Registered: $name at $addr:$port"
done
```

---

## Testing

```bash
make test NGINX_SRC=/path/to/nginx-1.26.1
```

The smoke test (`t/smoke.sh`) verifies:

1. `nginx -t` config validation passes
2. NGINX starts and the discovery endpoint responds
3. Both services appear in the JSON by name
4. Ports are correctly auto-discovered from `listen` (no overrides in test config)
5. Addresses are correctly auto-discovered from `server_name`
6. `Content-Type: application/json` header is set
7. Response has `{"services":[...]}` top-level shape

---

## Security Considerations

1. **Restrict the discovery endpoint.**  It exposes service names, FQDNs, and
   ports.  Limit access to trusted networks:

   ```nginx
   location /consul/services {
       consul_service_discovery on;
       allow 10.0.0.0/8;
       allow 127.0.0.1;
       deny  all;
   }
   ```

2. **Do not expose the endpoint on a public interface.**  Bind the discovery
   server block to a management IP or dedicated VLAN.

3. **`server_name` values are config-controlled strings.**  Do not embed
   credentials or sensitive data in `server_name` or `consul_service_tags`.

4. **The catalogue is a startup snapshot.**  After `nginx -s reload` the
   postconfiguration hook re-runs and the catalogue is refreshed automatically.

5. **No authentication on the endpoint itself.**  If mTLS is required, place
   the discovery server block behind an SSL listener with `ssl_verify_client on`.

---

## Known Limitations

| Limitation | Workaround |
|-----------|-----------|
| If a server has no `listen` directive, `port` is `0` and a `[warn]` line is emitted | Add an explicit `listen` or set `consul_service_port_override` |
| If a server listens on multiple ports, the first match in `cmcf->ports` is used | Set `consul_service_port_override` to select a specific port |
| If a server has no `listen` and no default port, `address` is `""` and `port` is `0` | Add an explicit `listen` directive |
| If a server listens on multiple addresses, the first match in `cmcf->ports` is used for the address | Set `consul_service_port_override` to clarify intent; address ambiguity has no override |
| Catalogue refreshes only on `nginx -s reload` | Automate reloads or run a sidecar that handles dynamic registration |

---

## References

- [Consul HTTP API — Service Registration](https://developer.hashicorp.com/consul/api-docs/agent/service)
- [NGINX HTTP Module Development Guide](https://nginx.org/en/docs/dev/development_guide.html)
- [NGINX `ngx_http_core_module` source — `ngx_http_add_listen`](https://github.com/nginx/nginx/blob/master/src/http/ngx_http.c)
- [`ngx_inet_get_port` — port byte-order conversion](https://github.com/nginx/nginx/blob/master/src/core/ngx_inet.c)

---

## License

Apache 2.0 — see [LICENSE](LICENSE).
