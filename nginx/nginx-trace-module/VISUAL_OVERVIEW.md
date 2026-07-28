# ngx-trace — visual overview

## What it does

```
                                    ┌─────────────────────────────────┐
   Client ──►  nginx worker          │  ngx_http_trace_module          │
               │                     │                                 │
               │ POST_READ  ──────── │  decide: trace this request?    │
               │ SERVER_REWRITE      │         │ yes                   │
               │ FIND_CONFIG         │         ▼                      │
               │ REWRITE             │  ┌──────────────┐              │
               │ PREACCESS           │  │ trace context │  r->pool     │
               │ ACCESS              │  │  · steps[]    │  auto-freed  │
               │ PRECONTENT          │  │  · vars[]     │              │
               │ CONTENT ──► proxy ──│──│  · tries[]    │              │
               │ LOG ────────────────│──│  · fault      │              │
               │                     │  │  · bodies     │              │
               │                     │  └──────┬───────┘              │
               │                     │         │ commit at LOG         │
               │                     │         ▼                      │
               │                     │  ┌──────────────┐              │
               │                     │  │  ring buffer  │  shm slab    │
               │                     │  │  64 slots     │  mutex-guard │
               │                     │  │  + session    │              │
               │                     │  │    store      │              │
               │                     │  └──────┬───────┘              │
               └─────────────────────┼─────────┼──────────────────────┘
                                     │         │
   Operator ──►  /__trace/ ──────────┼─────────┘
               │  · sessions CRUD
               │  · transactions list/detail
               │  · export / import / share
               │  · SPA UI
```

## Architecture layers

```
 Layer 1 ── Phase observers              automatic, always on
            POST_READ → REWRITE → ACCESS → CONTENT → LOG
            └─ records {phase, t_offset_us, vars, status}

 Layer 2 ── Handler interception          trace_intercept on
            wraps clcf->handler, saves orig, measures duration
            └─ records {handler: "proxy", duration_us: 342}

 Layer 3 ── Emit API                      ngx_trace_step()
            cooperating modules/scripts self-report steps
            └─ records {phase: "auth-oidc", note: "token valid"}

 Layer 4 ── eBPF add-on                   Rust/aya, future
            kernel-side uprobes on nginx + OpenSSL
```

## Configuration directives (17 total)

```
http {
    trace_zone          trace_zone 2m;      # ─ REQUIRED. shm zone for sessions+ring

    trace_max_sessions      32;             # ─ max concurrent sessions (hard limit: 32)
    trace_max_transactions  64;             # ─ max txns per session (hard limit: 64)
    trace_retention         24h;            # ─ how long stopped sessions stay viewable
    trace_intercept         on;             # ─ Layer-2: name the handler that ran
    trace_ebpf              off;            # ─ Layer-4: enable eBPF agent
    trace_hardened          off;            # ─ force body capture off everywhere
    trace_log               /tmp/trace.log; # ─ dedicated diagnostics log file
    trace_log_level         error;          # ─ off < error < warn < info < debug < trace

    server {
        trace on;                           # ─ enable tracing in this scope
        trace_watch $uri $status;           # ─ variables to snapshot per step
        trace_redact authorization cookie;  # ─ names to mask before shm storage
        trace_upstream_capture headers;     # ─ off | headers | full
        trace_body_capture both;            # ─ off | request | response | both
        trace_body_max 8192;                # ─ per-direction byte cap
        trace_fault_only on 502;            # ─ only capture faulting requests
        trace_grpc_proto /path/to/proto;    # ─ protobuf descriptor (future)

        location /__trace/ {
            trace_control;                  # ─ install API + UI handler
        }
    }
}
```

## Data flow: one traced request

```
  POST_READ          decide: traced ✓
  ─────────
  │ selector runs
  │ creates ctx in r->pool
  │ start_msec = now
  ▼

  REWRITE            step #0  {phase:"REWRITE", t_offset_us:0, vars:{uri:/foo}}
  ───────
  │ phase observer appends step
  │ snapshots $uri, $status...
  ▼

  ACCESS             step #1  {phase:"ACCESS", t_offset_us:125, vars:{uri:/foo}}
  ──────
  ▼

  PRECONTENT          wraps content_handler → orig_content_handler saved
  ──────────
  ▼

  CONTENT             step #2  {phase:"CONTENT", handler:"proxy", duration_us:342, timed:true}
  ───────                    ┌─ upstream capture:
  │ proxy_pass runs          │  try #0: {peer:"127.0.0.1:9000",
  │ captures upstream bytes  │           request:"GET /echo HTTP/1.1\r\n...",
  │                           │           response_headers:"HTTP/1.1 200 OK\r\n...",
  ▼                           │           status:200}
                              └─ body filter: copies response body prefix

  LOG                 step #3  {phase:"LOG", t_offset_us:1450}
  ───
  │ derives step statuses from final HTTP status
  │ detects fault (if 4xx/5xx)
  │ redacts Authorization/Cookie values
  │ serializes to JSON
  │ commits to ring buffer
  ▼
  ┌─────────────────────────────────────┐
  │  Ring buffer slot #12                │
  │  {"txn":"trace","method":"GET",     │
  │   "uri":"/foo","status":200,        │
  │   "worker_pid":42,"connection_id":3,│
  │   "steps":[...],                    │
  │   "upstream":{...},                 │
  │   "request_body":{...},             │
  │   "response_body":{...},            │
  │   "fault":{...}}                    │
  └─────────────────────────────────────┘
```

## API surface

```
POST   /__trace/sessions              create session    → 201 + TraceSession
GET    /__trace/sessions              list sessions     → {sessions:[...]}
GET    /__trace/sessions/{id}         session detail    → TraceSession
DELETE /__trace/sessions/{id}         stop session      → stopped_reason:"manual"

GET    /__trace/sessions/{id}/transactions        list tier   → TransactionSummary[]
GET    /__trace/sessions/{id}/transactions/{txn}  detail tier → Transaction

GET    /__trace/sessions/{id}/export    download artifact → {session, transactions:[...]}
GET    /__trace/sessions/{id}/share     deep link        → {url, session_id, expires_at}
POST   /__trace/import                 validate upload   → {imported, transactions:N}

GET    /__trace/ui                     SPA viewer
GET    /__trace/session?max=N&ttl=S   convenience create
GET    /__trace/last                   last committed txn
```

## JSON schema (what you see in the API)

```
Transaction {
  txn: "trace",
  method, uri, worker_pid, connection_id, status,
  steps: [{
    seq, phase, handler, t_offset_us, status, type,
    evaluated?, duration_us?, note?, timed?,
    vars: { "var_name": { value, op } }
  }],
  upstream?: {
    protocol, tries: [{
      seq, peer, status, bytes,
      connect_ms?, response_ms?,
      request, request_truncated,
      response_headers, response_truncated,
      grpc_status?, grpc_message?
    }]
  },
  fault?: {
    phase, handler, code, status, error_state, message, step_seq
  },
  request_body?: {
    captured_bytes, total_bytes, truncated,
    content_type?, preview? | preview_hex?
  },
  response_body?: { ...same shape... }
}
```

## UI layout (SPA at /__trace/ui)

```
┌─ bar ───────────────────────────────────────────────────┐
│ nginx trace  [sessions▾] [New] [Stop] [Refresh]        │
│ [Export] [Share] [Import]  [Search___________] [opts]  │
└─────────────────────────────────────────────────────────┘
┌─ rail ───┐ ┌─ center timeline ──────┐ ┌─ right detail ─────────┐
│           │ │                        │ │                        │
│ GET /      │ │ ▼ SERVER_REWRITE (1)   │ │ Step                   │
│  200 · ε  │ │   ✓ core         ε0µs  │ │  phase: SERVER_REWRITE │
│           │ │                        │ │  handler: core         │
│ POST /json│ │ ▼ REWRITE (1)          │ │  status: ✓ success     │
│  200 · 2ms│ │   ✓ core         ε0µs  │ │  offset: ε0µs          │
│           │ │                        │ │                        │
│ GET /fail │ │ ▶ ACCESS (3)          │ │ Variables              │
│  401      │ │   ✗ auth_request 1.2ms│ │  uri        read  /foo│
│  [fault]  │ │   ✓ auth_basic   ε0µs  │ │  status     set   200 │
│           │ │   ✗ core          ε0µs  │ │                        │
│           │ │                        │ │ Upstream (http)        │
│           │ │ ▶ CONTENT (1)         │ │  ▶ try 0 · 127.0.0.1   │
│           │ │   ✓ proxy       342µs  │ │    bytes: 368          │
│           │ │                        │ │    connect: 1ms        │
│           │ │ ▶ LOG (1)             │ │    response: 2ms       │
│           │ │   ✓ log          ε0µs  │ │    GET /echo HTTP/1.1 │
│           │ │                        │ │    ...                 │
│           │ │                        │ │                        │
│           │ │                        │ │ Request body           │
│           │ │                        │ │  captured: 15/15 bytes │
│           │ │                        │ │  {"key":"value"}       │
│           │ │                        │ │                        │
│           │ │                        │ │ Response body          │
│           │ │                        │ │  captured: 12/12 bytes │
│           │ │                        │ │  hello world           │
└───────────┘ └────────────────────────┘ └────────────────────────┘
```

## Memory layout

```
┌── request pool (r->pool, auto-freed at LOG) ─────────────────┐
│  ngx_http_trace_ctx_t                                        │
│  · start_msec, start_time                                    │
│  · steps  (ngx_array_t, max 64)                              │
│  · tries  (ngx_array_t, max 8)                               │
│  · req_body, resp_body (pool-allocated, max 2048 each)      │
│  · fault  (inline struct)                                    │
└──────────────────────────────────────────────────────────────┘

┌── shared memory zone (slab-allocated, mutex-guarded) ────────┐
│                                                              │
│  ring buffer: 64 slots × 8192 bytes = 512 KB                 │
│  ┌──────┬──────┬──────┬──────┬──────┬──────┬──────┬─────    │
│  │slot 0│slot 1│slot 2│slot 3│slot 4│ ...  │slot63│          │
│  │comm. │comm. │empty │comm. │comm. │      │empty │          │
│  └──────┴──────┴──────┴──────┴──────┴──────┴──────┴─────    │
│                                                              │
│  session store: 32 entries                                   │
│  ┌──────┬──────┬──────┬──────┬──────┬─────────              │
│  │sess 1│sess 2│free  │sess 4│free  │ ...                   │
│  │capt. │stopp.│      │capt. │      │                        │
│  └──────┴──────┴──────┴──────┴──────┴─────────              │
│                                                              │
│  zone: 2 MB total (default test size)                        │
└──────────────────────────────────────────────────────────────┘
```

## Fault classification

```
 HTTP status
     │
     ├── 401, 403 ──► access_denied
     ├── 404      ──► not_found
     ├── other 4xx──► client_error
     │
     ├── 5xx + upstream failed ──► upstream_error
     └── other 5xx ─────────────── server_error
```

## Test gates

```
 make test         31 files, 800 assertions, 39s
 make test-asan    ASan-clean, zero memory errors
 make test-matrix  1.27.0 ✓  1.26.2 ✓  1.24.0 ✓
```
