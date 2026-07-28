# ngx-trace

An Apigee-style request debugger for nginx — per-request phase timelines,
variable snapshots, byte-exact upstream capture, gRPC, fault detection,
session store, ring buffer, JSON API + SPA.

## Quick start

```sh
make up                    # nginx + httpbin on localhost:8080
curl -X POST localhost:8080/__trace/sessions
curl localhost:8080/httpbin/get?foo=bar
curl localhost:8080/__trace/sessions/1/transactions | python3 -m json.tool
open http://localhost:8080/__trace/ui
```

See [MANUAL_TEST.md](MANUAL_TEST.md) for the full feature-by-feature smoke guide.

## How it works

```
                                     ┌─────────────────────────────────┐
   Client ──►  nginx worker          │  ngx_http_trace_module          │
               │                     │                                 │
               │ POST_READ  ──────── │  decide: trace this request?    │
               │ SERVER_REWRITE      │         │ yes                   │
               │ FIND_CONFIG         │         ▼                       │
               │ REWRITE             │  ┌───────────────┐              │
               │ PREACCESS           │  │ trace context │  r->pool     │
               │ ACCESS              │  │  · steps[]    │  auto-freed  │
               │ PRECONTENT          │  │  · vars[]     │              │
               │ CONTENT ──► proxy ──│──│  · tries[]    │              │
               │ LOG ────────────────│──│  · fault      │              │
               │                     │  │  · bodies     │              │
               │                     │  └──────┬────────┘              │
               │                     │         │ commit at LOG         │
               │                     │         ▼                       │
               │                     │  ┌───────────────┐              │
               │                     │  │  ring buffer  │  shm slab    │
               │                     │  │  64 slots     │  mutex-guard │
               │                     │  │  + session    │              │
               │                     │  │    store      │              │
               │                     │  └──────┬────────┘              │
               └─────────────────────┼─────────┼───────────────────────┘
                                     │         │
   Operator ──►  /__trace/ ──────────┼─────────┘
               │  · sessions CRUD
               │  · transactions list/detail
               │  · export / import / share
               │  · SPA UI
```

### Three capture layers

```
 Layer 1 ── Phase observers              automatic, always on
             records {phase, t_offset_us, vars, status}

 Layer 2 ── Handler interception          trace_intercept on
             wraps clcf->handler, names the handler, measures duration

 Layer 3 ── Emit API                      ngx_trace_step()
             cooperating modules self-report substeps
```

### One traced request (data flow)

```
  POST_READ          decide: traced ✓
  REWRITE            step #0  {phase:"REWRITE", vars:{uri:/foo}}
  ACCESS             step #1  {phase:"ACCESS"}
  CONTENT            step #2  {phase:"CONTENT", handler:"proxy", duration_us:342}
                    ┌ upstream: {peer:"127.0.0.1:9000", request:"GET /...", status:200}
                    └ response body preview captured
  LOG                step #3  {phase:"LOG"}
                    → commit to ring buffer as JSON
```

## Configuration

```nginx
http {
    trace_zone          trace_zone 2m;      # REQUIRED — shm zone for sessions+ring
    trace_max_sessions      32;             # max concurrent sessions
    trace_max_transactions  64;             # max txns per session
    trace_retention         24h;            # viewable lifetime after stop
    trace_intercept         on;             # Layer-2 handler naming
    trace_hardened          off;            # force body capture off everywhere
    trace_log_level         error;          # off < error < warn < info < debug < trace

    server {
        trace on;                           # enable tracing
        trace_watch $uri $status $upstream_addr;
        trace_redact authorization cookie set-cookie;
        trace_upstream_capture headers;     # off | headers | full
        trace_body_capture both;            # off | request | response | both
        trace_body_max 8192;                # per-direction byte cap
        trace_fault_only on 502;            # only capture failing requests

        location /__trace/ {
            trace off;                      # never trace the API itself
            trace_control;                  # install API + UI handler
        }
    }
}
```

## API

```
POST   /__trace/sessions                         create → 201
GET    /__trace/sessions                         list
GET    /__trace/sessions/{id}                    detail
DELETE /__trace/sessions/{id}                    stop
GET    /__trace/sessions/{id}/transactions        summary list
GET    /__trace/sessions/{id}/transactions/{txn}  full detail
GET    /__trace/sessions/{id}/export              JSON artifact
GET    /__trace/sessions/{id}/share               deep link
POST   /__trace/import                           validate upload
GET    /__trace/ui                               SPA viewer
GET    /__trace/session?max=N&ttl=S             convenience create
GET    /__trace/last                             last committed txn
```

## JSON schema

```
Transaction {
  txn, method, uri, worker_pid, connection_id, status,
  steps: [{ seq, phase, handler, t_offset_us, status, type,
            evaluated?, duration_us?, note?, timed?,
            vars: { "name": { value, op } } }],
  upstream?: { protocol, tries: [{ seq, peer, status, bytes,
            connect_ms?, response_ms?, request, response_headers,
            request_truncated, response_truncated,
            grpc_status?, grpc_message? }] },
  fault?: { phase, handler, code, status, error_state, message, step_seq },
  request_body?: { captured_bytes, total_bytes, truncated,
            content_type?, preview? | preview_hex? },
  response_body?: { ... }
}
```

## Memory layout

```
 request pool (auto-freed)              shared memory zone (2 MB)
 ┌─────────────────────────┐           ┌──────────────────────────┐
 │ ctx: steps(64), tries(8),│          │ ring: 64 slots × 8 KB     │
 │      bodies(2×2KB),     │           │ sessions: 32 entries      │
 │      fault               │           │ mutex-guarded             │
 └─────────────────────────┘           └──────────────────────────┘
```

## Fault classification

```
 401, 403 → access_denied    4xx → client_error    5xx+upstream → upstream_error
 404     → not_found                                5xx          → server_error
```

## Test gates

```
 make test         31 files, 800 assertions    ~39s
 make test-asan    zero memory errors
 make test-matrix  1.27.0 ✓  1.26.2 ✓  1.24.0 ✓
```

## Documents

| File | Purpose |
|---|---|
| [IDEA.md](IDEA.md) | Vision, motivation, future ideas |
| [SPEC.md](SPEC.md) | Normative requirements (FR-*/NFR-*/CON-*) |
| [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) | Milestones M0–M10, divergence register |
| [REVIEW.md](REVIEW.md) | Code review findings, gap analysis |
| [MANUAL_TEST.md](MANUAL_TEST.md) | Manual smoke testing guide |
| [VISUAL_OVERVIEW.md](VISUAL_OVERVIEW.md) | Full visual overview (diagrams, schema, UI) |
| [DEV_PROCESS.md](DEV_PROCESS.md) | Multi-agent development pipeline |
| [AGENTS.md](AGENTS.md) | Guidelines for AI agents |

## Make targets

| Command | What |
|---|---|
| `make test` | Run 800-test suite |
| `make test-one T=t/file.t` | Run single test file |
| `make test-asan` | Suite under AddressSanitizer |
| `make test-matrix` | Suite on 1.27.0, 1.26.2, 1.24.0 |
| `make test-valgrind` | Suite under Valgrind memcheck |
| `make up` | Start nginx + httpbin on localhost:8080 |
| `make down` | Stop services |
| `make sh` | Shell in dev container (ports 8080, 9000) |
| `make bench` | 10s wrk smoke test |
