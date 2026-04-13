# ngx_http_greylist_module

A **native C dynamic module** for open-source NGINX that provides
pattern-based rate limiting with automatic client greylisting.

A full reimplementation of
[fabriziofiorucci/NGINX-Greylist](https://github.com/fabriziofiorucci/NGINX-Greylist)
that requires **no NGINX Plus**, no NJS, and no `keyval_zone`.

## How it works

```
  Client request
       │
       ▼
  ┌────────────────────────────────────────────────────────┐
  │  ACCESS phase  (ngx_http_greylist_module)              │
  │                                                        │
  │  1. Compute fingerprint                                │
  │     IP | FNV32(User-Agent) | FNV32(Bearer-token)       │
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

## Feature parity with the original

| Original (NGINX Plus + NJS)         | This module (open-source NGINX)         |
|-------------------------------------|-----------------------------------------|
| `keyval_zone` shared memory         | Slab-allocated shared memory + rbtree   |
| NJS `clientFingerprint()` (FNV-1a)  | Identical FNV-1a 32-bit hash in C       |
| `auth_request` greylist check       | Inline ACCESS phase — no subrequest     |
| NJS `addToGreylist()` on 429        | Same phase, zero overhead               |
| NJS `denyGreylisted()` response     | `Retry-After` + `X-Greylisted` headers  |
| `keyval_zone timeout=` (LRU / GC)   | LRU queue + configurable GC timeout     |
| `limit_req_zone` leaky bucket       | Same algorithm, implemented in C        |
| NGINX Plus R24+ required            | Open-source NGINX ≥ 1.9.11              |
| NJS module required                 | No extra modules needed                 |

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
    --add-dynamic-module=/path/to/nginx-greylist-module
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

    # 3. Define rate-limit rules
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
            # 4. Enable enforcement
            greylist zone=gl;
            proxy_pass http://backend;
        }
    }
}
```

## Directive reference

### `greylist_zone`

```
Syntax:   greylist_zone <name> <size> [timeout=<N>[s]];
Context:  http
```

Creates a named shared-memory zone that holds greylist entries and
rate-limit token buckets.

| Parameter | Description |
|---|---|
| `name` | Identifier used by `greylist_rule` and `greylist` |
| `size` | Shared memory size — e.g. `16m`. 1 MB ≈ 6 000 entries |
| `timeout` | Hard GC age for LRU entries (default `3600s`). Set this to at least your longest `duration=` value |

### `greylist_rule`

```
Syntax:   greylist_rule zone=<name>
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

Common pattern examples:

```nginx
# Match any host
pattern="~*^POST:https?://[^/]+/auth/login"

# Match a specific host
pattern="~*^POST:https?://api\.example\.com/auth/login"

# Match multiple methods
pattern="~*^(POST|PUT):https?://[^/]+/api/items"

# Exact match (no regex)
pattern="GET:http://localhost/healthz"
```

### `greylist`

```
Syntax:   greylist zone=<name>;
Context:  http, server, location
```

Enables the greylist access check in the current context. The `greylist`
directive can appear in an included file even when `greylist_zone` is
defined in the outer `nginx.conf` — directive order does not matter.

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

Each client is identified by a fingerprint string:

```
<IP>|<fnv32hex(User-Agent)>|<fnv32hex(Bearer-token)>
```

Examples:
```
10.0.0.5|a1b2c3d4|00000000          # no auth token
10.0.0.5|a1b2c3d4|cafebabe          # with bearer token
2001:db8::1|deadbeef|cafebabe       # IPv6
```

This is identical to the `clientFingerprint()` function in the original
NJS implementation:

- IP is kept raw for auditability.
- User-Agent and Bearer token are hashed with FNV-1a 32-bit to bound key
  length regardless of how long those strings are.
- Clients behind the same NAT IP but with different User-Agents or tokens
  are tracked independently.
- Unauthenticated clients are fingerprinted by IP + UA only (token hash
  of `""` = `811c9dc5`).

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
existing greylist remains intact — equivalent to `keyval_zone state=`
persistence in the original.

### OOM behaviour

If the slab allocator runs out of shared memory, the module **fails open**:
the request is allowed through and a `WARN` log entry is written. This
prevents a memory exhaustion from becoming a service outage.

### Worker safety

All shared-memory access is protected by `ngx_shmtx_lock`. PCRE matching
is performed *outside* the mutex (PCRE is not reentrant under a lock).

## Logging

The module logs at `WARN` level on every block and greylist write. To see
these messages, set:

```nginx
error_log /var/log/nginx/error.log warn;
```

Typical entries:

```
# Rule matched but within rate limit
greylist: RL rule=0 excess=500 burst=5000 — pass

# Rate limit exceeded, client greylisted
greylist: RL rule=0 excess=6000 burst=5000 — BLOCK
greylist: GREYLISTED fp="10.0.0.5|a1b2c3d4|00000000" rule=0 duration=120s

# Subsequent requests from same client
greylist: BLOCKED fp="10.0.0.5|a1b2c3d4|00000000" retry_after=118s
```

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `unknown directive "greylist_zone"` | Module not loaded | Add `load_module modules/ngx_http_greylist_module.so;` before `http{}` |
| `nginx: [emerg] module ... is not binary compatible` | Module built against wrong nginx version | Rebuild against the exact version from `nginx -v` |
| All requests pass (no 429) | Pattern does not match | Check the `subject=` in warn logs vs your pattern |
| Pattern never matches | Quotes not stripped or regex typo | Verify pattern with `error_log ... warn;` — logged as `regex "..." vs "..."` |
| `greylist_rule: zone "X" not found` | Rule defined before zone | Move `greylist_zone` before `greylist_rule`, or just reload (lookup is deferred) |
| Zone OOM | `size` too small | Increase `greylist_zone` size or decrease `timeout=` |

### Pattern debugging checklist

The match subject is always: `METHOD:scheme://host/uri[?args]`

```
POST:http://127.0.0.1/auth/login          ← HTTP
POST:https://api.example.com/auth/login   ← HTTPS
GET:http://127.0.0.1/api/search?q=foo
DELETE:https://api.example.com/api/admin/users
```

Common regex mistakes:

| Wrong | Right | Issue |
|---|---|---|
| `http?://` | `https?://` | `?` makes `p` optional, not `s` |
| `[^/]` | `[^/]+` | matches exactly 1 char, not the whole hostname |
| `https://` | `https?://` | doesn't match plain HTTP |
| `pattern=~*^POST` | `pattern="~*^POST"` | must quote the value |

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
#           (6th request exceeds burst=5, gets greylisted)

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

Based on the design of
[fabriziofiorucci/NGINX-Greylist](https://github.com/fabriziofiorucci/NGINX-Greylist).
