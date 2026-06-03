# ngx_http_consul_service_discovery

A native C NGINX module that exposes selected `server {}` blocks as a JSON
service catalogue via a configurable location endpoint.  It does **not** talk
to Consul directly; a sidecar, cron job, or Consul agent can poll the endpoint
and register/deregister services through the
[Consul HTTP API](https://developer.hashicorp.com/consul/api-docs/agent/service).

**Port and address are discovered automatically** from the `listen` and
`server_name` directives — no manual override directives are required for
standard deployments.

Compatible with **NGINX Open Source ≥ 1.13** (static and dynamic module
builds).

---

## Table of Contents

1. [Features](#features)
2. [How Auto-Discovery Works](#how-auto-discovery-works)
3. [Architecture](#architecture)
4. [Quick Start](#quick-start)
5. [Build Instructions](#build-instructions)
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

- **Automatic port discovery** — walks `cmcf->ports` at postconfiguration time
- **Automatic address discovery** — bind IP from the `listen` directive
- **NAT / load-balancer friendly** — `consul_service_port_override` for external ports
- **Tags as JSON array** — split from comma-separated `consul_service_tags`
- **Explicit service ID** — `consul_service_id`; auto-generated as `<name>-<first-tag>` when unset
- **Metadata** — repeatable `consul_service_meta key value;` → JSON object
- **Health checks** — repeatable `consul_service_check name=… tcp|http|grpc=… interval=… timeout=…;` → JSON array
- **Zero Consul dependency** — expose service metadata from NGINX config alone
- **Standard NGINX build system** — static and dynamic builds supported

---

## How Auto-Discovery Works

### Port

During the NGINX configuration parse, every `listen` directive inserts an
entry into `cmcf->ports`.  The module's `postconfiguration` hook (which runs
before `ngx_http_optimize_servers`) walks
`ports → addrs → servers[]`, matching each `ngx_http_core_srv_conf_t *` by
pointer equality to find the listen port in host byte order.

### Address

The `address` field is the bind IP obtained by calling
`ngx_sock_ntop(opt.sockaddr, opt.socklen, buf, len, 0)` on the matched
`ngx_http_conf_addr_t`.

| `listen` directive        | `address` in JSON |
| ------------------------- | ----------------- |
| `listen 80;`              | `"0.0.0.0"`       |
| `listen 1.2.3.4:80;`      | `"1.2.3.4"`       |
| `listen [::]:80;`         | `"::"`            |
| `listen 127.0.0.1:8080;`  | `"127.0.0.1"`     |

---

## Architecture

```
NGINX config parse
  │  every listen directive → ngx_http_add_listen()
  ▼
postconfiguration hook  (before ngx_http_optimize_servers)
  │  walk cmcf->ports → addrs → servers[]
  │  match cscf by pointer → port, address
  │  split consul_service_tags CSV → tag array
  │  build id (explicit or "<name>-<first-tag>")
  │  copy meta k/v pairs, check structs
  ▼
in-memory service catalogue  (ngx_array_t, permanent pool)
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
    server {
        listen      10.0.1.5:18880;
        server_name api.example.com;

        consul_service_discoverable on;
        consul_service_name         "my-api";
        consul_service_id           "my-api-v2";
        consul_service_tags         "v2,production,https";

        consul_service_meta env  prod;
        consul_service_meta team platform;

        consul_service_check name=tcp  tcp=10.0.1.5:18880  interval=10s  timeout=1s;

        location / { proxy_pass http://backend; }
    }
}
```

```
curl -s http://discovery.internal:8080/consul/services | jq .
```

```json
{
  "services": [
    {
      "id": "my-api-v2",
      "name": "my-api",
      "address": "10.0.1.5",
      "port": 18880,
      "tags": ["v2", "production", "https"],
      "meta": { "env": "prod", "team": "platform" },
      "checks": [
        { "name": "tcp", "tcp": "10.0.1.5:18880", "interval": "10s", "timeout": "1s" }
      ]
    }
  ]
}
```

---

## Build Instructions

### Prerequisites

- NGINX source tree (same version as the installed binary for dynamic builds)
- GCC or Clang
- PCRE, OpenSSL, zlib development headers

### Dynamic Module (Recommended)

```bash
# 1. Match the installed NGINX version
nginx_version=$(nginx -v 2>&1 | grep -oP '[\d.]+' | head -n1)
wget https://nginx.org/download/nginx-${nginx_version}.tar.gz
tar xzf nginx-${nginx_version}.tar.gz

# 2. Clone this module
git clone https://github.com/nginx/nginx-demos.git

# 3. Build the .so
cd nginx-${nginx_version}
./configure \
    --with-compat          \
    --with-http_ssl_module \
    --with-http_v2_module  \
    --add-dynamic-module=../nginx-demos/nginx/ngx_http_consul_service_discovery_module
make modules -j$(nproc)

# 4. Install
sudo cp objs/ngx_http_consul_service_discovery_module.so /etc/nginx/modules/
```

Add to `nginx.conf` (top level, before `events {}`):

```nginx
load_module modules/ngx_http_consul_service_discovery_module.so;
```

### Static Module

```bash
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

```
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

Enables the JSON catalogue endpoint for this location.

---

### `consul_service_discoverable`

**Syntax:** `consul_service_discoverable on | off;`
**Default:** `off`
**Context:** `server`

Marks this virtual server as a discoverable service.

---

### `consul_service_name`

**Syntax:** `consul_service_name "<name>";`
**Default:** first `server_name` value
**Context:** `server`

Logical service name in the JSON `name` field.

---

### `consul_service_id`

**Syntax:** `consul_service_id "<id>";`
**Default:** `"<name>-<first-tag>"`  (auto-generated)
**Context:** `server`

Explicit service ID.  When omitted the module generates `<name>-<first-tag>`
(e.g. `api1-v1`).  If there are no tags the `name` is used as the `id`.

```nginx
consul_service_id "payments-v2-prod";
```

---

### `consul_service_tags`

**Syntax:** `consul_service_tags "<tag1,tag2,...>";`
**Default:** `"nginx"`
**Context:** `server`

Comma-separated tag string.  The module splits it into a JSON **array** in
the response.  Leading and trailing spaces around each tag are stripped.

```nginx
consul_service_tags "v2,production,https";
# → "tags": ["v2", "production", "https"]
```

---

### `consul_service_meta`

**Syntax:** `consul_service_meta <key> <value>;`
**Default:** *(none)*
**Context:** `server`
**Repeatable:** yes

Adds a key/value pair to the `meta` object.  Repeat for multiple entries.

```nginx
consul_service_meta env  prod;
consul_service_meta team payments;
# → "meta": {"env":"prod","team":"payments"}
```

---

### `consul_service_check`

**Syntax:** `consul_service_check key=value ...;`
**Default:** *(none)*
**Context:** `server`
**Repeatable:** yes

Adds a health check entry to the `checks` array.  Each token is a `key=value`
pair.  Recognised keys:

| Key        | Description                                       |
| ---------- | ------------------------------------------------- |
| `name`     | Human-readable check name                        |
| `tcp`      | Sets check type to TCP; value is `host:port`      |
| `http`     | Sets check type to HTTP; value is the URL         |
| `grpc`     | Sets check type to gRPC; value is `host:port`     |
| `ttl`      | Sets check type to TTL; value is the TTL duration |
| `interval` | How often to run the check (e.g. `10s`)           |
| `timeout`  | Per-check timeout (e.g. `1s`)                     |

Exactly one of `tcp`, `http`, `grpc`, or `ttl` must be present.  Its name
becomes the key in the JSON check object, matching the Consul API convention.

```nginx
consul_service_check name=tcp  tcp=10.0.1.5:18880  interval=10s  timeout=1s;
consul_service_check name=web  http=http://10.0.1.5/health  interval=15s  timeout=2s;
# → "checks": [
#     {"name":"tcp","tcp":"10.0.1.5:18880","interval":"10s","timeout":"1s"},
#     {"name":"web","http":"http://10.0.1.5/health","interval":"15s","timeout":"2s"}
#   ]
```

---

### `consul_service_port_override`

**Syntax:** `consul_service_port_override <port>;`
**Default:** *(port auto-discovered from `listen`)*
**Context:** `server`

Overrides the advertised port.  Useful behind NAT or a public load balancer.

---

### `consul_service_address_override`

**Syntax:** `consul_service_address_override "<address>";`
**Default:** *(address auto-discovered from `listen`)*
**Context:** `server`

Overrides the advertised address.  Essential in three common situations:

| Situation | Auto-discovered | Should advertise |
| --------- | --------------- | ---------------- |
| Wildcard bind (`listen 8080;`) | `0.0.0.0` — unroutable | real host IP |
| Container / VM (`listen 172.17.0.2:8080;`) | container IP | host IP or VIP |
| Multi-homed host, data-plane on eth1 | management IP | data-plane IP |

```nginx
server {
    listen      0.0.0.0:18880;
    consul_service_discoverable on;
    consul_service_name         "api1";
    consul_service_address_override "10.0.1.5";   # real routable IP
    consul_service_port_override    18880;
}
```

Consul's own [best-practice guidance](https://developer.hashicorp.com/consul/docs/services/configuration/services-configuration-reference#address)
states that `address` should be the IP or hostname clients actually use to
reach the service, not the bind address.

---

## JSON Response Format

`GET /consul/services` → `200 OK`, `Content-Type: application/json`:

```json
{
  "services": [
    {
      "id":      "api1-v1",
      "name":    "api1",
      "address": "10.0.1.5",
      "port":    18880,
      "tags":    ["v1", "public", "http"],
      "meta":    { "env": "prod", "team": "payments" },
      "checks":  [
        { "name": "tcp", "tcp": "10.0.1.5:18880", "interval": "10s", "timeout": "1s" }
      ]
    },
    {
      "id":      "api2-v2",
      "name":    "api2",
      "address": "10.0.1.6",
      "port":    80,
      "tags":    ["v2", "admin", "nat"],
      "meta":    { "env": "staging" }
    }
  ]
}
```

| Field     | Source                                                | Notes                                           |
| --------- | ----------------------------------------------------- | ----------------------------------------------- |
| `id`      | `consul_service_id` or `<name>-<first-tag>`           | always present                                  |
| `name`    | `consul_service_name` or first `server_name`          |                                                 |
| `address` | `consul_service_address_override` or bind IP from `listen` directive                       | `"0.0.0.0"` for wildcard                        |
| `port`    | `listen` directive or `consul_service_port_override`  | host byte order                                 |
| `tags`    | `consul_service_tags` split on `,`                    | JSON **array**; was a plain string in v1        |
| `meta`    | `consul_service_meta` pairs                           | JSON object; omitted if no directives set       |
| `checks`  | `consul_service_check` entries                        | JSON array; omitted if no directives set        |

---

## Configuration Examples

### Full example with all new fields

```nginx
http {
    server {
        listen 8080;
        location /consul/services {
            consul_service_discovery on;
            allow 10.0.0.0/8;
            deny  all;
        }
    }

    server {
        listen      10.0.1.5:18880;
        server_name api1.example.com;

        consul_service_discoverable on;
        consul_service_name         "api1";
        consul_service_id           "api1-v1";
        consul_service_tags         "v1,public,http";
        consul_service_meta         env  prod;
        consul_service_meta         team payments;
        consul_service_check name=tcp  tcp=10.0.1.5:18880  interval=10s  timeout=1s;

        location / { proxy_pass http://api1-backend; }
    }

    server {
        listen      10.0.1.6:80;
        server_name api2.example.com;

        consul_service_discoverable on;
        consul_service_name         "api2";
        consul_service_tags         "v2,admin,nat";
        consul_service_meta         env  staging;

        location / { proxy_pass http://api2-backend; }
    }
}
```

Result:

```json
{"services":[
  {"id":"api1-v1","name":"api1","address":"10.0.1.5","port":18880,
   "tags":["v1","public","http"],"meta":{"env":"prod","team":"payments"},
   "checks":[{"name":"tcp","tcp":"10.0.1.5:18880","interval":"10s","timeout":"1s"}]},
  {"id":"api2-v2","name":"api2","address":"10.0.1.6","port":80,
   "tags":["v2","admin","nat"],"meta":{"env":"staging"}}
]}
```

### NAT / load-balancer port override

```nginx
server {
    listen      8080;
    server_name internal-svc.corp;

    consul_service_discoverable  on;
    consul_service_name          "internal-svc";
    consul_service_tags          "v2,internal";
    consul_service_port_override 80;
}
```

### Consul agent sidecar registration

See `examples/register-services.sh` — it polls the NGINX catalogue and
pushes directly to `PUT /v1/agent/service/register`, passing all fields
including `id`, the tags array, meta object, and checks array verbatim.

---

## Testing

```bash
make test NGINX_SRC=/path/to/nginx-1.26.1
```

The smoke test (`t/smoke.sh`) verifies all eleven assertions:

1. `nginx -t` config validation passes
2. NGINX starts and the discovery endpoint responds
3. Both services appear in the JSON by name
4. Ports are correctly auto-discovered from `listen`
5. Addresses are correctly auto-discovered
6. `Content-Type: application/json` header is set
7. Response has `{"services":[...]}` top-level shape
8. `tags` is a JSON **array** (not a plain string)
9. `id` is present and correct (explicit and auto-generated)
10. `meta` object is correct when `consul_service_meta` is set
11. `checks` array is present and carries the check type key

---

## Security Considerations

1. **Restrict the discovery endpoint** — it exposes names, addresses, and ports:

```nginx
location /consul/services {
    consul_service_discovery on;
    allow 10.0.0.0/8;
    allow 127.0.0.1;
    deny  all;
}
```

2. **Do not expose on a public interface** — bind to a management IP.
3. **`server_name` and meta values are config-controlled strings** — do not embed credentials.
4. **Catalogue is a startup snapshot** — refreshed automatically on `nginx -s reload`.
5. **No built-in authentication** — use `ssl_verify_client` for mTLS if required.

---

## Known Limitations

| Limitation | Workaround |
| ---------- | ---------- |
| Multiple listen ports per server → first match used | Set `consul_service_port_override` |
| Multiple listen addresses per server → first match used | Use `consul_service_address_override` or one address per server block |
| Catalogue refreshes only on `nginx -s reload` | Automate reloads or use a sidecar |
| `meta` values are always strings | Consul's API expects `map[string]string` — this is correct |
| No `deregister_critical_service_after` in checks | Add a wrapper in the sidecar script |

---

## References

- [Consul HTTP API — Service Registration](https://developer.hashicorp.com/consul/api-docs/agent/service)
- [NGINX HTTP Module Development Guide](https://nginx.org/en/docs/dev/development_guide.html)
- [NGINX `ngx_http_core_module` source](https://github.com/nginx/nginx/blob/master/src/http/ngx_http.c)

---

## License

Apache 2.0 — see [LICENSE](LICENSE).
