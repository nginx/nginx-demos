# Manual testing guide

Two modes: single-container (`make sh`) or compose with httpbin (`make up`).

## Compose mode (recommended)

```sh
make up             # starts nginx + httpbin
make down           # stops both
```

- nginx   → `localhost:8080`
- httpbin → `localhost:9001` (direct) or via proxy at `localhost:8080/httpbin/`
- trace UI → `http://localhost:8080/__trace/ui`

## Single-container mode

```sh
make sh             # shell in container, ports 8080 and 9000 bound to host

# Inside container:
nginx -c /src/ngx-trace/docker/smoke.conf &
```

Built-in echo backend on `:9000`, traced routes on `:8080`.

## Quick smoke

```sh
# 1. Create a session
curl -s -X POST localhost:8080/__trace/sessions
# → {"id":1,"state":"capturing",...}

# 2. Drive traced traffic
curl localhost:8080/hello
curl localhost:8080/proxy/foo
curl localhost:8080/fail
curl localhost:8080/sub

# 3. List captured transactions
curl -s localhost:8080/__trace/sessions/1/transactions | python3 -m json.tool

# 4. Drill into one transaction
curl -s localhost:8080/__trace/sessions/1/transactions/1 | python3 -m json.tool

# 5. Export session
curl -s localhost:8080/__trace/sessions/1/export > session.json

# 6. Stop session
curl -s -X DELETE localhost:8080/__trace/sessions/1
```

## Feature-by-feature

### Module inert (M0)

```sh
curl localhost:8080/hello
# → "hello from ngx-trace"                     # no trace, module just loaded
```

### Session lifecycle (M6)

```sh
curl -s -X POST localhost:8080/__trace/sessions?max=10\&ttl=120
# → 201 Created                                   # TTL = 120s, max 10 txns

curl -s localhost:8080/__trace/sessions
# → {"sessions":[{id:1,state:"capturing",...}]}

curl -s -X DELETE localhost:8080/__trace/sessions/1
# → state:"stopped", stopped_reason:"manual"
```

### Phase timeline + variable watch (M2)

```sh
curl -s -X POST localhost:8080/__trace/sessions
curl localhost:8080/hello
curl -s localhost:8080/__trace/last | python3 -m json.tool
# Look for: steps[] with POST_READ, REWRITE, ACCESS, CONTENT, LOG
# Each step has vars: {uri, status, upstream_addr, request_method}
```

### Step status derivation (M2.6)

```sh
curl -s -X POST localhost:8080/__trace/sessions
curl localhost:8080/fail
curl -s localhost:8080/__trace/last | python3 -m json.tool
# Steps leading to the 401 have status:"error"
```

### Fault detection (M4)

```sh
curl -s -X POST localhost:8080/__trace/sessions
curl localhost:8080/fail
curl -s localhost:8080/__trace/last | python3 -m json.tool
# → "fault":{"phase":"...","status":401,"error_state":"access_denied","step_seq":...}
```

### Fault-only session (M4.3)

```sh
curl -s -X POST "localhost:8080/__trace/sessions?fault_only=true"
curl localhost:8080/hello                            # success → discarded
curl localhost:8080/fail                             # fault → captured
curl -s localhost:8080/__trace/sessions/2/transactions | python3 -m json.tool
# → 1 transaction (hello was discarded)
```

### Body capture (M8)

```sh
curl -s -X POST localhost:8080/__trace/sessions
curl -s -X POST -d '{"key":"value"}' localhost:8080/json
curl -s localhost:8080/__trace/last | python3 -m json.tool
# → "request_body":{"captured_bytes":...,"preview":"{\"key\":\"value\"}"}
# → "response_body":{"captured_bytes":...,"preview":"{\"status\":\"ok\"}"}
```

### Upstream capture (M3)

```sh
curl -s -X POST localhost:8080/__trace/sessions
curl localhost:8080/proxy/some/path
curl -s localhost:8080/__trace/last | python3 -m json.tool
# → "upstream":{"protocol":"http","tries":[{
#       "peer":"127.0.0.1:9000",
#       "request":"GET /some/path HTTP/1.1\r\nHost: ...\r\n...",
#       "response_headers":"HTTP/1.1 200 OK\r\nX-Echo-Uri: /some/path\r\n...",
#       "status":200
#     }]}
```

### Subrequest correlation (M8.4)

```sh
curl -s -X POST localhost:8080/__trace/sessions
curl localhost:8080/sub
curl -s localhost:8080/__trace/last | python3 -m json.tool
# steps[] includes: {"phase":"SUBREQUEST","type":"subrequest","note":"/auth-check"}
```

### Redaction (M8.0)

```sh
curl -s -X POST localhost:8080/__trace/sessions
curl -H "Authorization: Bearer secret-token" localhost:8080/hello
curl -s localhost:8080/__trace/last | python3 -m json.tool
# Authorization header shows [REDACTED] in upstream request and variable values
```

### Layer-2 intercept (M7)

```sh
curl -s -X POST localhost:8080/__trace/sessions
curl localhost:8080/proxy/test
curl -s localhost:8080/__trace/last | python3 -m json.tool
# CONTENT step has handler:"proxy" (not empty), with duration_us measured
```

### Emit API (M9)

```sh
# Convenience routes for collectors
curl -s "localhost:8080/__trace/session?max=8&ttl=60&path=/proxy/" | python3 -m json.tool
curl -s localhost:8080/__trace/last | python3 -m json.tool
# → the most recent committed transaction
```

### Hardened mode (M8.6)

Edit `docker/smoke-httpbin.conf`, add `trace_hardened on;` in the http block, rebuild.
Body capture is force-disabled everywhere and raw upstream bytes never reach the error_log.

### httpbin routes (compose mode)

```sh
curl -s -X POST localhost:8080/__trace/sessions
curl localhost:8080/httpbin/get?foo=bar
curl localhost:8080/httpbin/post -X POST -d '{"k":"v"}'
curl localhost:8080/httpbin/status/418
curl localhost:8080/httpbin/headers -H "X-Custom: hello"
curl localhost:8080/httpbin/redirect/2
curl localhost:8080/httpbin/delay/1
curl -s localhost:8080/__trace/sessions/1/transactions | python3 -m json.tool
```

httpbin provides richer upstream scenarios — 4xx/5xx status codes, redirect chains,
variable response sizes, custom headers, delays.

### SPA UI (M6.6/M8.5)

```sh
open http://localhost:8080/__trace/ui
# Left panel: session selector + live transaction list
# Center: timeline grouped by phase, status icons (✓/✗/⊖/⊟), ε marker
# Right: step detail, variable snapshot, upstream, body previews, fault
# Search bar highlights matches, auto-expands groups
# View options persisted in localStorage
# Share copies deep link, Import renders offline
```

### Cross-worker read (M5.6)

```sh
# Edit docker/smoke-httpbin.conf: worker_processes 4;
# Rebuild, make up, drive traffic — any worker's capture visible via any request
```

## Config file

Two configs are available:

| File | Mode | Use |
|---|---|---|
| `docker/smoke.conf` | `make sh` | Single-container with built-in echo backend |
| `docker/smoke-httpbin.conf` | `make up` | Compose with go-httpbin + echo backend |

Both set: `trace on`, `trace_intercept on`, `trace_watch $uri $status $upstream_addr $request_method`, `trace_redact authorization cookie set-cookie`, `trace_body_capture both` on most routes.
