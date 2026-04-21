# ngx_http_greylist_module

A **native C dynamic module** for open-source NGINX that provides
pattern-based rate limiting with automatic client greylisting.

## How it works

```
  Client request
       │
       ▼
  ┌────────────────────────────────────────────────────────┐
  │  ACCESS phase  (ngx_http_greylist_module)              │
  │                                                        │
  │  1. Compute fingerprint                                │
  │     Concatenation of FNV32 hashes of each source       │
  │     defined by greylist_fingerprint_header directives  │
  │     (HTTP headers and/or NGINX variables)              │
  │                │                                       │
  │  2. Lookup fingerprint in greylist                     │
  │     (shared-memory red-black tree)                     │
  │                │                                       │
  │      active? ──┴──► 429 + Retry-After                  │
  │                │                                       │
  │  3. Find first matching rate-limit rule                │
  │     subject: "METHOD:scheme://host/uri"                │
  │                │                                       │
  │      no match ─┴──► pass through                       │
  │                │                                       │
  │  4. Leaky-bucket check  (per client, per rule)         │
  │                │                                       │
  │      OK ───────┴──► pass through                       │
  │                │                                       │
  │      exceeded ─┴──► write greylist entry (TTL)         │
  │                      429 + Retry-After                 │
  └────────────────────────────────────────────────────────┘
       │
       ▼
  proxy_pass → upstream
```

## Requirements

| Requirement | Version |
|---|---|
| NGINX (open-source) | ≥ 1.9.11 |
| PCRE or PCRE2 | bundled with most distros |
| OpenSSL | only if using HTTPS |

> **NGINX Plus is not required.**
> NJS is not required.

## Installation

### Option A — Build from source

```bash
# 1. Match the nginx version you are running
NGINX_VER=$(nginx -v 2>&1 | grep -oP '[\d.]+' | head -n1)

# 2. Download nginx source
wget https://nginx.org/download/nginx-${NGINX_VER}.tar.gz
tar -xzf nginx-${NGINX_VER}.tar.gz

# 3. Build only the dynamic module (fast — does not rebuild nginx itself)
cd nginx-${NGINX_VER}
./configure \
    --with-compat          \
    --with-http_ssl_module \
    --with-http_v2_module  \
    --add-dynamic-module=../nginx-demos/nginx/ngx_http_greylist_module
make modules -j$(nproc)

# 4. Install
sudo cp objs/ngx_http_greylist_module.so /etc/nginx/modules/

# 5. Validate and reload
sudo nginx -t && sudo nginx -s reload
```

> **Important:** Always compile against the exact nginx version you are
> running. ABI compatibility across major versions is not guaranteed even
> with `--with-compat`.

### Option B — Docker Compose

```bash
git clone https://github.com/nginx/nginx-demos
cd nginx/ngx_http_greylist_module
docker compose up --build
```

The included `Dockerfile` compiles the module and bakes it into the official
`nginx` image — no manual steps required.

## Quick-start configuration

```nginx
# nginx.conf ──────────────────────────────────────────────────────────────────

# 1. Load the module (must be before http{})
load_module modules/ngx_http_greylist_module.so;

http {

    # 2. Create a shared-memory zone
    greylist_zone  gl  16m  timeout=3600s;

    # 3. Define what makes up the client fingerprint for this zone.
    #    There are no built-in components — you choose exactly what to hash.
    #    Use var= for NGINX variables and header= for HTTP request headers.
    greylist_fingerprint_header zone=gl var=$remote_addr;
    greylist_fingerprint_header zone=gl var=$http_user_agent;

    # 4. Define rate-limit rules
    greylist_rule  zone=gl
                   pattern="~*^POST:https?://[^/]+/auth/login"
                   rate=5r/s
                   burst=5
                   duration=120s;

    greylist_rule  zone=gl
                   pattern="~*^GET:https?://[^/]+/api/search"
                   rate=30r/m
                   burst=10
                   duration=60s;

    server {
        listen 80;

        location / {
            # 5. Enable enforcement
            greylist zone=gl;
            proxy_pass http://backend;
        }
    }
}
```

## Directive reference

### `greylist_zone`

```
Syntax:   greylist_zone <n> <size> [timeout=<N>[s]];
Context:  http
```

Creates a named shared-memory zone that holds greylist entries and
rate-limit token buckets.

| Parameter | Description |
|---|---|
| `name` | Identifier used by `greylist_rule`, `greylist`, and `greylist_fingerprint_header` |
| `size` | Shared memory size — e.g. `16m`. 1 MB ≈ 6 000 entries |
| `timeout` | Hard GC age for LRU entries (default `3600s`). Set this to at least your longest `duration=` value |

---

### `greylist_rule`

```
Syntax:   greylist_rule zone=<n>
                        pattern="[~[*]]<string>"
                        rate=<N>r/[sm]
                        burst=<N>
                        duration=<N>[s];
Context:  http
```

Adds one rate-limit rule to the named zone.

Multiple rules can reference the same zone. Rules are evaluated in order;
the **first matching rule** per request is applied.

| Parameter | Description |
|---|---|
| `zone` | Zone name — must already be defined with `greylist_zone` |
| `pattern` | Match pattern (see below) |
| `rate` | Allowed request rate: `5r/s` (per second) or `30r/m` (per minute) |
| `burst` | Extra requests above the rate before rejection (`0` = no tolerance) |
| `duration` | How long to greylist an offending client, in seconds (default `60`) |

#### Pattern syntax

The **match subject** built from every request is:

```
METHOD:scheme://host/uri[?args]
```

For example: `POST:https://api.example.com/auth/login`

| Prefix | Meaning | Example |
|---|---|---|
| `~*` | Case-insensitive PCRE regex | `~*^POST:https?://[^/]+/auth/login` |
| `~` | Case-sensitive PCRE regex | `~^GET:http://internal\.corp/` |
| _(none)_ | Exact string match | `GET:http://localhost/ping` |

> **Always quote the pattern value** — e.g. `pattern="~*^POST:..."`.
> nginx keeps the surrounding `"` when the token starts with `p`; the
> module strips them automatically.

---

### `greylist`

```
Syntax:   greylist zone=<n>;
Context:  http, server, location
```

Enables the greylist access check in the current context.

---

### `greylist_fingerprint_header`

```
Syntax:   greylist_fingerprint_header zone=<n> header=<Header-Name>;
          greylist_fingerprint_header zone=<n> var=<$variable>;
Context:  http
```

Adds one source to the client fingerprint for the named zone. The directive
is **repeatable** — each occurrence appends one more source, evaluated in
declaration order.

**There are no built-in fingerprint components.** The fingerprint is
entirely operator-defined through these directives. A zone with no
`greylist_fingerprint_header` directives will emit a startup `WARN` and
pass all requests through unchecked.

#### Parameters

Exactly one of `header=` or `var=` must be given per directive. They are
mutually exclusive.

| Parameter | Description |
|---|---|
| `zone=<n>` | Zone to attach this source to (required) |
| `header=<n>` | Value of the named HTTP **request** header |
| `var=<$varname>` | Value of the named NGINX **variable** (leading `$` is optional) |

#### How the fingerprint is built

Each source contributes one FNV-1a 32-bit hash segment to the fingerprint
string. Segments are pipe-separated:

```
<hash0>|<hash1>|<hash2>...
```

For example, with `var=$remote_addr` and `header=X-Device-Id`:

```
c0a80101|deadbeef
```

When a header is absent or a variable is unset/empty, the FNV hash of the
empty string (`811c9dc5`) is contributed — the fingerprint length stays
constant regardless of whether optional headers are present.

#### `header=` sources

The header name is matched **case-insensitively**. Both of the following
are equivalent:

```nginx
greylist_fingerprint_header zone=gl header=X-Real-IP;
greylist_fingerprint_header zone=gl header=x-real-ip;
```

> **Security note:** because HTTP headers are controlled by the client or
> an upstream proxy, only include headers that are set or overwritten by
> a trusted proxy. Never use a header that end-users can forge.

#### `var=` sources

Any NGINX variable can be used — built-in variables, variables set by
`set`, `map`, `geo`, `geoip`, `realip`, etc.

```nginx
greylist_fingerprint_header zone=gl var=$remote_addr;
greylist_fingerprint_header zone=gl var=$binary_remote_addr;
greylist_fingerprint_header zone=gl var=$http_user_agent;
greylist_fingerprint_header zone=gl var=$ssl_client_fingerprint;
```

The leading `$` is optional: `var=remote_addr` and `var=$remote_addr` are
equivalent.

Variable indices are resolved at **config-parse time** via
`ngx_http_get_variable_index()` so there is no name lookup on the request
hot path.

#### Examples

**Replicate the original NJS fingerprint** (IP + User-Agent + Bearer token)
using variables:

```nginx
greylist_fingerprint_header zone=gl var=$remote_addr;
greylist_fingerprint_header zone=gl var=$http_user_agent;
greylist_fingerprint_header zone=gl var=$http_authorization;
```

**Behind a reverse proxy** — use the real client IP from the proxy header,
plus User-Agent:

```nginx
greylist_fingerprint_header zone=gl header=X-Real-IP;
greylist_fingerprint_header zone=gl var=$http_user_agent;
```

**Per-device tracking** — add a device identity header:

```nginx
greylist_fingerprint_header zone=gl var=$remote_addr;
greylist_fingerprint_header zone=gl header=X-Device-Id;
```

**API key only** — track entirely by API key, ignoring IP:

```nginx
greylist_fingerprint_header zone=gl header=X-Api-Key;
```

**mTLS deployment** — fingerprint on the client TLS certificate:

```nginx
greylist_fingerprint_header zone=gl var=$ssl_client_fingerprint;
```

**Combine multiple zones** — each zone has an independent fingerprint:

```nginx
greylist_zone  api  16m;
greylist_zone  web  8m;

# API zone: IP + API key
greylist_fingerprint_header zone=api var=$remote_addr;
greylist_fingerprint_header zone=api header=X-Api-Key;

# Web zone: IP + User-Agent
greylist_fingerprint_header zone=web var=$remote_addr;
greylist_fingerprint_header zone=web var=$http_user_agent;
```

## Response headers

When a client is rate-limited or greylisted, the module sets:

| Header | Value | Description |
|---|---|---|
| `Retry-After` | seconds | Remaining greylist duration |
| `X-Greylisted` | `1` | Present on every 429 from this module |

You can customise the response body using `error_page`:

```nginx
error_page 429 @greylisted;
location @greylisted {
    default_type application/json;
    return 429 '{"error":"Too Many Requests","message":"See Retry-After header."}';
}
```

## Client fingerprint

The fingerprint is a pipe-separated string of FNV-1a 32-bit hash segments,
one per configured `greylist_fingerprint_header` source:

```
<fnv32hex(source-0)>|<fnv32hex(source-1)>|...
```

There are **no built-in components**. If you want the behaviour of the
original NJS `clientFingerprint()` function (IP | FNV32(UA) | FNV32(Bearer)),
configure it explicitly:

```nginx
greylist_fingerprint_header zone=gl var=$remote_addr;
greylist_fingerprint_header zone=gl var=$http_user_agent;
greylist_fingerprint_header zone=gl var=$http_authorization;
```

### Fingerprint format

| Sources configured | Example fingerprint |
|---|---|
| `var=$remote_addr` | `c0a80101` |
| `var=$remote_addr` + `var=$http_user_agent` | `c0a80101\|a1b2c3d4` |
| `header=X-Api-Key` | `cafebabe` |
| `var=$remote_addr` + `header=X-Device-Id` | `c0a80101\|deadbeef` |

When a source is absent (header missing, variable unset), FNV(`""`) =
`811c9dc5` is contributed.

## Auto-expiry

Greylist entries expire through two independent mechanisms — no cron job
or API call required.

```
  t=0          t=now >= expiry       t=expiry + timeout
  ───────────────────────────────────────────────────────►
  │                    │                        │
  Written by      Logical expiry:          LRU GC:
  rate limit      handler evicts           entry freed,
  exceeded        entry, allows            memory
                  client through           reclaimed
```

1. **Logical expiry** — the ACCESS handler compares `ngx_time()` to the
   stored epoch. Once past, the entry is removed from the rbtree and the
   client is allowed through immediately.

2. **LRU GC** — when slab memory runs low, the oldest entries are evicted.
   The `timeout=` parameter on `greylist_zone` sets the maximum age before
   forced eviction, regardless of expiry.

## Adding a new rule

Example: rate-limit `PUT /api/items` at 10 req/min, greylist 90 s.

**Step 1** — add to `http{}` in `nginx.conf`:

```nginx
greylist_rule  zone=gl
               pattern="~*^PUT:https?://[^/]+/api/items"
               rate=10r/m
               burst=3
               duration=90s;
```

**Step 2** — reload:

```bash
sudo nginx -t && sudo nginx -s reload
```

That's it. No location block changes needed — the rule is evaluated
for every request in any location that has `greylist zone=gl;`.

## Runtime behaviour

### Reload safety

Greylist entries are stored in shared memory and survive a graceful
`nginx -s reload`. Workers pick up the new configuration while the
existing greylist remains intact.

### OOM behaviour

If the slab allocator runs out of shared memory, the module **fails open**:
the request is allowed through and a `WARN` log entry is written. This
prevents a memory exhaustion from becoming a service outage.

### Worker safety

All shared-memory access is protected by `ngx_shmtx_lock`. PCRE matching
is performed *outside* the mutex (PCRE is not reentrant under a lock).

### Zones with no fingerprint sources

If a zone has no `greylist_fingerprint_header` directives, NGINX will emit
a `WARN` at startup and all requests to that zone will be passed through
unchecked. This is a fail-open safety measure — add at least one
`greylist_fingerprint_header` directive to enable enforcement.

## Logging

The module logs at `WARN` level on every block and greylist write. To see
these messages, set:

```nginx
error_log /var/log/nginx/error.log warn;
```

Typical entries:

```
# Fingerprint source registered at startup
greylist_fingerprint_header: zone "gl" +var "$remote_addr" (idx=0)
greylist_fingerprint_header: zone "gl" +header "x-device-id"

# Rule matched but within rate limit
greylist: RL rule=0 excess=500 burst=5000 — pass

# Rate limit exceeded, client greylisted
greylist: RL rule=0 excess=6000 burst=5000 — BLOCK
greylist: GREYLISTED fp="c0a80101|a1b2c3d4" rule=0 duration=120s

# Subsequent requests from same client
greylist: BLOCKED fp="c0a80101|a1b2c3d4" retry_after=118s
```

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `unknown directive "greylist_zone"` | Module not loaded | Add `load_module modules/ngx_http_greylist_module.so;` before `http{}` |
| `nginx: [emerg] module ... is not binary compatible` | Module built against wrong nginx version | Rebuild against the exact version from `nginx -v` |
| All requests pass (no 429) | No fingerprint sources configured | Add `greylist_fingerprint_header` directives for the zone |
| All requests pass (no 429) | Pattern does not match | Check the `subject=` in warn logs vs your pattern |
| `greylist_fingerprint_header: header= and var= are mutually exclusive` | Both parameters on one line | Use one directive per source |
| `greylist_fingerprint_header: zone "X" not found` | Zone not yet defined | Move `greylist_zone` before `greylist_fingerprint_header` |
| `greylist_rule: zone "X" not found` | Rule defined before zone | Move `greylist_zone` first, or just reload (lookup is deferred) |
| Zone OOM | `size` too small | Increase `greylist_zone` size or decrease `timeout=` |

### Pattern debugging checklist

The match subject is always: `METHOD:scheme://host/uri[?args]`

```
POST:http://127.0.0.1/auth/login          ← HTTP
POST:https://api.example.com/auth/login   ← HTTPS
GET:http://127.0.0.1/api/search?q=foo
DELETE:https://api.example.com/api/admin/users
```

## Testing

```bash
# Start the demo stack
docker compose up --build -d

# Run the smoke test suite
./tests/smoke.sh http://localhost

# Manual test: trigger rate limit
for i in $(seq 1 10); do
  printf "Request %2d: " "$i"
  curl -s -o /dev/null -w "%{http_code}\n" \
    -X POST -H "User-Agent: TestClient/1.0" \
    http://localhost/auth/login
done
# Expected: 200 200 200 200 200 200 429 429 429 429

# Check the Retry-After header
curl -si -X POST -H "User-Agent: TestClient/1.0" \
  http://localhost/auth/login | grep -i "retry-after\|x-greylisted"

# Verify a different User-Agent has its own independent counter
curl -s -o /dev/null -w "%{http_code}\n" \
  -X POST -H "User-Agent: OtherClient/2.0" \
  http://localhost/auth/login
# → 200  (fresh counter for this fingerprint)
```

## Architecture notes

### Why not use `ngx_http_limit_req_module`?

The standard `limit_req_zone` module provides per-key rate limiting but
has no concept of greylisting: once a request is rejected the client
counter resets at the next window and the client can try again
immediately. This module adds a **persistent penalty period** — the client
is blocked for `duration=` seconds regardless of how many requests it
sends.

### Why a native C module instead of NJS?

| | NJS + keyval | This module |
|---|---|---|
| NGINX edition | Plus only | Open-source |
| Extra modules | `ngx_http_js_module`, `ngx_http_auth_request_module` | None |
| Subrequests | Yes (`auth_request`) | No — inline access phase |
| Regex engine | NJS built-in | PCRE/PCRE2 (same as nginx) |
| Shared memory | `keyval_zone` (Plus API) | Slab + rbtree (standard) |
| Performance | JS interpreter overhead | Native C, zero overhead |

## License

Apache 2.0 — see [LICENSE](LICENSE).
