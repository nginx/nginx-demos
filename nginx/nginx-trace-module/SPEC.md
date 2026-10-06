# ngx-trace — Technical Specification

> **Status:** Specification v0.2 — reconciled against the M0–M10 implementation
> **Scope:** The `ngx_http_trace_module` (C core). The optional Rust/`aya` eBPF
> add-on (Layer 4) is specified only at its interface contract; its internals are
> out of scope for this document.
> **Reconciliation:** §4.1 defaults/contexts, §8.3 schema shapes, §9 API request
> grammar and §5.1 phase coverage now describe what the code actually does. Every
> place this document changed to follow the implementation — rather than the
> implementation changing to follow this document — is itemised in
> `IMPLEMENTATION_PLAN.md` §10 ("Spec/implementation divergence register") for
> later adjudication. Entries there marked **spec-was-right** are open defects,
> not settled decisions.
> **Last updated:** 2026-07-26

---

## 0. Conventions

The key words **MUST**, **MUST NOT**, **REQUIRED**, **SHALL**, **SHALL NOT**,
**SHOULD**, **SHOULD NOT**, **RECOMMENDED**, **MAY**, and **OPTIONAL** in this
document are to be interpreted as described in RFC 2119.

- **Requirement IDs** use the form `FR-<area>-<n>` (functional),
  `NFR-<area>-<n>` (non-functional), and `CON-<area>-<n>` (constraints). They are
  stable references for tests and traceability.
- "The module" means `ngx_http_trace_module` (the C core) unless stated otherwise.
- "Transaction" = the recorded timeline of one `ngx_http_request_t`.
- Section cross-references in the form `IDEA §N` point to `IDEA.md`.

---

## 1. Overview & goals

`ngx-trace` is an nginx module plus a companion JSON API and single-page UI that
captures, per request, an ordered, timestamped timeline of nginx's internal
processing — phases, handlers ("plugins"), variable/header changes, the exact
upstream request/response (incl. gRPC), client bodies, step status, and faults —
following Apigee's time-boxed, list-first, detail-on-demand Debug model.

### 1.1 Specification goals (normative summary)

| ID | Goal |
|----|------|
| FR-CORE-1 | The module MUST produce a per-request, ordered, timestamped timeline of nginx phases and the handlers that ran within each, recording each handler's return code and duration. |
| FR-CORE-2 | The module MUST snapshot a configurable watch-list of variables and headers per step and MUST classify each variable as `read`, `set`, or `set_failed`. |
| FR-CORE-3 | The module MUST capture the exact request sent to and response received from the upstream, per connection try, via deep hooks (not solely `$upstream_*`). Headers MUST be byte-exact; bodies MUST be optional and size-capped. |
| FR-CORE-4 | The module MUST support optional, size-capped, redaction-aware capture of the client request body and client response body, independently controllable. |
| FR-CORE-5 | The module MUST allow starting/stopping trace sessions on live traffic without a configuration reload. |
| FR-CORE-6 | The module MUST expose a JSON API and a static UI; sessions MUST be exportable as a single JSON artifact. |
| FR-CORE-7 | The module MUST record fault information (phase, handler, finalizing status, `error_state`) for denied/errored requests and MUST support fault-based session filtering. |
| NFR-PERF-1 | Non-traced requests MUST incur near-zero overhead (no per-step allocation or evaluation). |
| NFR-SEC-1 | Sensitive headers/variables/bodies MUST be redacted before entering shared memory; control endpoints MUST be access-controlled. |

### 1.2 Out of scope (v1)

- The `stream` (L4 TCP/UDP) module lifecycle (CON-SCOPE-1).
- Request/response mutation from the UI (observe-only) (CON-SCOPE-2).
- Live config editing (CON-SCOPE-3).
- eBPF add-on internals (interface only; CON-SCOPE-4).
- Distributed/multi-instance aggregation beyond the optional external collector
  interface (CON-SCOPE-5).

---

## 2. Definitions

| Term | Definition |
|------|------------|
| Phase | One of nginx's fixed HTTP phases `POST_READ`…`LOG`. |
| Handler | A module callback registered in a phase; the unit shown as a "plugin" step. |
| Filter | A header/body output-chain callback. |
| Upstream | The backend nginx proxies to (`proxy_pass`, `fastcgi_pass`, `grpc_pass`, …). |
| Subrequest | An internally generated child request (`auth_request`, `mirror`, SSI). |
| Trace session | A time-boxed, count-limited, filtered capture window. |
| Transaction | The full recorded timeline of one request (detail tier). |
| TransactionSummary | The lightweight list-row for a captured request (list tier). |
| Step | One recorded entry in a transaction (phase/handler/filter/upstream event). |
| Step status | `success` / `error` / `skipped` / `disabled`. |
| Condition step | An `if`/`map`/`try_files`/redirect decision with its evaluated boolean. |
| Fault | The handler/phase/status that denied or errored a request. |
| Properties | A step's snapshot of nginx internal state. |
| Epsilon (ε) | UI marker for a step whose elapsed time is below 1 ms. |

---

## 3. Architecture (normative structure)

The module is composed of the following units. Each unit's requirements are
specified in the referenced section.

| Unit | Responsibility | Spec |
|------|----------------|------|
| Selector | At `POST_READ`, decide whether a request is traced. | §7 |
| Phase observers | One pass-through handler registered in every phase. | §5 |
| Interceptor (Layer 2) | Optional wrapping of handler/content/filter pointers. | §5.4 |
| Upstream capture | Wrap `u->create_request`/`u->process_header`/etc. | §6 |
| Body capture | Client request/response body capture. | §6.4 |
| Emit API (Layer 3) | `ngx_trace_step()` + `njs`/Lua binding. | §5.5 |
| Trace context | Per-request state in `r->pool`. | §8.1 |
| Session store | Shared-memory sessions + transaction ring buffer. | §8.2 |
| Control plane | JSON API + static UI content handlers. | §9, §10 |
| eBPF interface | Contract for the optional Layer-4 agent. | §11 |

### 3.1 Component invariants

- CON-ARCH-1: The module MUST register handlers only in phases that accept custom
  handlers. For `FIND_CONFIG`, `POST_REWRITE`, and `POST_ACCESS`, transitions
  MUST be inferred from surrounding observable state (IDEA §5.2).
- CON-ARCH-2: All per-request trace state MUST be allocated from `r->pool` (or a
  subpool of it) so it is freed automatically at request end.
- CON-ARCH-3: Completed transactions MUST be committed to the shared-memory zone
  exactly once, at the `LOG` phase.
- CON-ARCH-4: The C core MUST NOT have any build-time or run-time dependency on
  the eBPF add-on.

---

## 4. Configuration requirements

### 4.1 Directives

The module MUST implement the following directives. Contexts and defaults are
normative.

| ID | Directive | Context | Default | Requirement |
|----|-----------|---------|---------|-------------|
| FR-CFG-1 | `trace_zone <name> <size>` | http | — (required to enable) | Allocate a shared-memory zone for sessions + ring buffer. If absent, the module MUST be inert. |
| FR-CFG-2 | `trace on\|off` | http, server, location | `off` | Permit tracing for matching requests in scope. |
| FR-CFG-3 | `trace_watch <var> ...` | http, server, location | empty | Variables to snapshot per step. |
| FR-CFG-4 | `trace_control` | location | absent | Expose JSON API + UI at this location. Takes **no arguments**: presence installs the control content handler on that location. There is no `off` form — to disable, remove the directive. |
| FR-CFG-5 | `trace_intercept on\|off` | http | `off` | Enable Layer-2 per-module handler wrapping; when `off`, capture MUST degrade to phase-level (Layer 1). |
| FR-CFG-6 | `trace_upstream_capture full\|headers\|off` | http, server, location | `headers` | Depth of upstream capture (`full` = headers + capped bodies; `headers` = headers only; `off` = `$upstream_*` only). |
| FR-CFG-7 | `trace_grpc_proto <file>` | http, server, location | unset | Optional protobuf descriptor for gRPC payload decoding. |
| FR-CFG-8 | `trace_ebpf off\|on [tls]` | http | `off` | Enable the Layer-4 eBPF agent; `tls` allows kernel-side plaintext capture. |
| FR-CFG-9 | `trace_max_sessions <n>` | http | `4` | Max concurrent active sessions. Bounded above by the compile-time ceiling in §4.3. |
| FR-CFG-10 | `trace_max_transactions <n>` | http | `64` | Hard global ceiling per session regardless of the session's requested max. Default is the ring capacity (`NGX_HTTP_TRACE_RING_SLOTS`); bounded above by it (max 64). |
| FR-CFG-11 | `trace_body_capture off\|request\|response\|both` | http, server, location | `off` | Enable size-capped capture of client request/response bodies. |
| FR-CFG-12 | `trace_body_max <size>` | http, server, location | `8k` | Per-body capture cap (client and upstream). |
| FR-CFG-13 | `trace_redact <name> ...` | http, server, location | `authorization cookie set-cookie proxy-authorization` | Header/variable names to mask. A leading `$` is accepted and ignored, so the same directive names variables and headers alike. |
| FR-CFG-14 | `trace_retention <time>` | http | `24h` | How long completed sessions remain viewable before eviction. |
| FR-CFG-15 | `trace_log <path>\|off` | http | `off` | Enable module self-diagnostics logging (its own processing) to a dedicated file; `off` routes to nginx's `error_log` only. See §13.1. |
| FR-CFG-16 | `trace_log_level <level>` | http | `error` | Verbosity of self-diagnostics: `off` < `error` < `warn` < `info` < `debug` < `trace`. See §13.1. |
| FR-CFG-20 | `trace_fault_only on\|off [<code>]` | http, server, location | `off` | Restrict capture in this scope to requests that finalize as a fault; the optional argument narrows it to one HTTP status (`100`..`599`, validated at config time). Drives the provisional-commit path in FR-SEL-4. |
| FR-CFG-21 | `trace_hardened on\|off` | http | `off` | Hardened-mode kill switch (NFR-SEC-7). When `on`, body capture is force-disabled regardless of any `trace_body_capture` in any scope, and raw captured bytes are never written to the error log. Main context only — a security posture that could be relaxed per-location would be worthless. |

### 4.2 Configuration behavior

- FR-CFG-17: Merge of `trace`, `trace_watch`, `trace_upstream_capture`,
  `trace_body_capture`, `trace_body_max`, and `trace_redact` across
  http/server/location MUST follow nginx's standard inheritance (more specific
  scope overrides).
- FR-CFG-18: All unset directive fields MUST be initialized to a sentinel
  (`NGX_CONF_UNSET*`) in `create_*_conf` and resolved in `merge_*_conf`.
- FR-CFG-19: Trace **sessions** (filter, count, and the fault predicate) MUST be
  creatable via the API without a reload (§9); directives configure only static,
  per-location capabilities and global limits.
- CON-CFG-1: If `trace_zone` is not configured, all directives MUST be accepted
  (no config error) but the module MUST perform no capture and the API MUST
  return `503`.

### 4.3 Compile-time ceilings

The shared zone is a fixed-layout slab, so several limits are structural rather
than configurable. Runtime directives are clamped to these. They are normative
because they bound what an operator can actually request.

| Constant | Value | Meaning |
|----------|-------|---------|
| `NGX_HTTP_TRACE_MAX_SESSIONS` | `32` | Hard ceiling on the session table; `trace_max_sessions` cannot exceed it. |
| `NGX_HTTP_TRACE_RING_SLOTS` | `64` | Ring capacity (NFR-PERF-4); the oldest transaction is evicted past this. |
| `NGX_HTTP_TRACE_SLOT_MAX` | `8192` | Max serialized JSON bytes stored per transaction. |
| `NGX_HTTP_TRACE_MAX_STEPS` | `64` | Max recorded steps per transaction. |

- CON-CFG-2: A transaction whose serialized JSON would exceed
  `NGX_HTTP_TRACE_SLOT_MAX` MUST NOT be stored partially. The current
  implementation drops it and records the drop only in the diagnostics log; this
  is a known gap (`REVIEW.md` #6) rather than an intended contract.

---

## 5. Phase observation & per-module attribution

### 5.1 Phase observers (Layer 1) — REQUIRED

- FR-PHASE-1: The module MUST register one handler in each phase that accepts
  custom handlers. As implemented that is `POST_READ` (the selector),
  `SERVER_REWRITE`, `REWRITE`, `PREACCESS`, `ACCESS`, `PRECONTENT`, and `LOG`.
  `CONTENT` is deliberately **not** given a phase handler: pushing one onto
  `phases[CONTENT].handlers` is inert whenever a location sets `clcf->handler`
  (the common case, including `proxy_pass`), so content attribution is obtained
  by wrapping the content-handler slot in Layer 2 (§5.4) instead.
- FR-PHASE-2: Each observer handler MUST return `NGX_DECLINED` (except the
  selector logic in `POST_READ`, which MUST NOT alter control flow either) so the
  module never changes request routing or outcome.
- FR-PHASE-3: Each observer MUST record `{phase, t_offset_us}` and a snapshot of
  the watch-listed variables/headers as they are at the start of that phase, and
  MUST append a step to the trace context only when the request is traced.
- FR-PHASE-4: For phases without custom-handler support (`FIND_CONFIG`,
  `POST_REWRITE`, `POST_ACCESS`), the module MUST infer their effect from
  observable deltas (e.g. `$uri`/location change) and record inferred steps.

### 5.2 Step status & conditions — REQUIRED

- FR-STATUS-1: Each step MUST carry a `status` of `success`, `error`, `skipped`,
  or `disabled`, derived per IDEA §3.2 mapping.
- FR-STATUS-2: `if`/`map`/`try_files`/internal-redirect decisions MUST be
  recorded as steps of `type: "condition"` with an `evaluated` boolean and, where
  available, the expression text.
- FR-STATUS-3: A step MUST be marked `error` when the request is finalized with a
  4xx/5xx attributable to that phase/handler, and the fault (§6.5 / §8) MUST be
  populated accordingly.

### 5.3 Variable read/set semantics — REQUIRED

- FR-VAR-1: For each watched variable at each step, the module MUST record an
  object `{ "value", "op" }`.
- FR-VAR-2: `op` MUST be one of: `read` (evaluated/read only), `set` (assigned a
  value, Apigee `=`), or `set_failed` (assignment attempted but not applied —
  read-only or evaluation error, Apigee `≠`).
- FR-VAR-3: The module MUST force-evaluate watched variables that are lazily
  evaluated, without triggering side effects beyond variable evaluation.

### 5.4 Generic C-handler interception (Layer 2) — OPTIONAL

- FR-L2-1: When `trace_intercept on`, after `postconfiguration` (i.e. after all
  other modules have registered), the module SHOULD walk
  `cmcf->phases[phase].handlers`, the content handler slot (`clcf->handler`), and
  the top header/body filter chains, wrapping each function pointer with a
  trampoline that records start → calls original → records return code + duration.
- FR-L2-2: A wrapping trampoline MUST preserve the original return code exactly,
  including `NGX_AGAIN`/`NGX_DONE` suspend/resume semantics, so behavior is
  unchanged.
- FR-L2-3: The module MUST resolve a wrapped pointer to a human name using, in
  order: a curated symbol table, the module list (`cf->cycle->modules`), and
  optionally `dladdr()`. If none resolve, the step MUST fall back to a
  phase-level label.
- FR-L2-4: When `trace_intercept off`, the module MUST rely solely on Layer 1
  (phase-level + effect inference) and MUST NOT wrap any pointers.
- CON-L2-1: Layer-2 wrapping MUST be gated by an nginx-version compatibility
  check; on an unsupported version the module MUST refuse to wrap and log a
  warning, degrading to Layer 1.

### 5.5 Emit API (Layer 3) — OPTIONAL

- FR-L3-1: The module MUST export a public function, conceptually
  `ngx_trace_step(r, name, result, detail)`, that appends a self-described step to
  the current request's trace context if (and only if) the request is traced.
- FR-L3-2: `ngx_trace_step()` MUST be a no-op (O(1), no allocation) when the
  request is not traced.
- FR-L3-3: The module SHOULD provide an `njs`/Lua binding `trace.step(name, ...)`
  mapping to `ngx_trace_step()`.

---

## 6. Upstream & body capture

### 6.1 Upstream request/response (target) capture — REQUIRED

- FR-UP-1: For upstream-framework content handlers (`proxy_pass`, `fastcgi_pass`,
  `uwsgi_pass`, `scgi_pass`, `grpc_pass`, `memcached_pass`), the module MUST
  capture, per connection try: the chosen upstream and resolved peer, the exact
  request line + headers sent, and the response status line + headers received.
- FR-UP-2: The request line/headers MUST be obtained by wrapping
  `u->create_request` (and `u->reinit_request` for retries) and reading the
  serialized `u->request_bufs` buffer chain — the byte-exact bytes nginx writes.
- FR-UP-3: The response status/headers MUST be obtained by wrapping
  `u->process_header` and snapshotting the raw `u->buffer` region for byte-exact
  headers, in addition to parsed `u->headers_in`.
- FR-UP-4: Per-try timings and retry state MUST be recorded from the `u->state`
  array (connect/header/response times, bytes, status).
- FR-UP-5: Upstream capture hooks MUST be installed by saving and replacing the
  module's own `u->` callback pointers at request time and MUST NOT modify any
  upstream module's source.
- FR-UP-6: The `$upstream_*` variables MAY be read as enrichment/fallback but
  MUST NOT be the sole source when `trace_upstream_capture` is `headers` or
  `full`.
- FR-UP-7: When `trace_upstream_capture off`, or when the content handler does
  not use the upstream framework, the module MUST degrade gracefully to
  `$upstream_*` variables read at the log phase.

### 6.2 gRPC (`grpc_pass`) — REQUIRED (first-class)

- FR-GRPC-1: The module MUST detect gRPC traffic by content-type and MUST
  classify the upstream protocol as `"grpc"` in the schema (FR-JSON-3).
- FR-GRPC-2: The module MUST capture HTTP/2 **trailers** and MUST surface
  `grpc-status`/`grpc-message` as the authoritative step result, distinct from
  the HTTP `:status`.

### 6.3 Retries — REQUIRED

- FR-RETRY-1: Each connection attempt MUST appear as a distinct entry in
  `upstream.tries[]` with its own captured request, response, and `u->state`
  timing.

### 6.4 Client body capture — OPTIONAL

- FR-BODY-1: When `trace_body_capture` includes `request`, and the request body
  is (or will be) read by an existing consumer, the module MUST snapshot up to
  `trace_body_max` bytes from `r->request_body` (buffered `bufs` or `temp_file`)
  and MUST set `truncated` when the cap is exceeded.
- FR-BODY-2: The module MUST NOT force a request-body read that would change
  behavior; if nothing would otherwise read the body, capture MUST be a no-op
  unless the operator explicitly opts in (documented as potentially altering
  timing for large uploads).
- FR-BODY-3: Request-body capture MUST NOT block the event loop. As implemented,
  the snapshot is taken at `LOG`, by which point any consumer that was going to
  read the body already has — so the module never issues a read of its own and
  the asynchronous-completion pattern is not needed on this path.
- FR-BODY-4: When `trace_body_capture` includes `response`, the module MUST copy
  up to `trace_body_max` bytes of the response body via its output body filter,
  record `Content-Encoding`, set `truncated` past the cap, and finalize at
  `last_buf`.
- FR-BODY-5: Bodies MUST be stored as a size-capped preview: UTF-8 text in
  `preview`, binary in `preview_hex`; `content_type`, `captured_bytes`,
  `total_bytes`, and `truncated` MUST always be recorded.
- FR-BODY-6: The module registers at the **head** of the output filter chain, so
  the response body it observes is the body *before* `gzip` and other body filters
  transform it. Response previews are therefore **pre-compression plaintext**, and
  `content_encoding` MUST be read at `LOG` rather than at header-filter time
  (where downstream filters have not yet set it).
- FR-BODY-7: When response buffers are file-backed (`sendfile`/`in_file`, i.e. a
  static file served from disk), the module MUST report `total_bytes` honestly but
  MUST NOT capture the bytes, because reading them would mean blocking file I/O
  inside a filter (G8). `captured_bytes` MUST be `0` in that case.

### 6.5 Fault capture — REQUIRED

- FR-FAULT-1: On a denied/errored request, the module MUST populate
  `summary.fault` with `phase`, `handler`, `code`, `status` (finalizing HTTP
  status), `error_state`, `message`, and `step_seq` linking to the exact step.
- FR-FAULT-2: The fault MUST be determinable at or before the `LOG` phase so it
  can drive fault-only session commit (§7).

---

## 7. Request selection & performance

- FR-SEL-1: At `POST_READ`, the module MUST determine whether any active,
  non-expired session's filter matches the request and whether `trace on` is in
  effect for the resolved location.
- FR-SEL-2: If not selected, the request context MUST be marked "no-trace" and
  every later observer MUST early-return with no allocation, evaluation, or
  step recording (NFR-PERF-1).
- FR-SEL-3: If selected but the session already reached `max_transactions`
  (bounded by `trace_max_transactions`), the request MUST NOT be captured.
- FR-SEL-4: For fault-only sessions (`filter.fault_only`/`fault_code`), the
  module MUST provisionally record into the request-pool context and commit to the
  ring buffer at `LOG` **only if** the finalized fault matches; otherwise it MUST
  discard the provisional data.
- FR-SEL-5: The session store MUST expose `active_since` reflecting when the
  session became visible to all workers; a brief cross-worker activation window
  MUST be tolerated (no crash, no partial-session corruption).
- NFR-PERF-2: Watch-list snapshots MUST evaluate only named variables, never the
  full variable set.
- NFR-PERF-3: A completed transaction MUST be copied into the ring buffer at most
  once.
- NFR-PERF-4: The ring buffer MUST be bounded; when full, the oldest transaction
  MUST be evicted.
- NFR-PERF-5: Completed sessions MUST remain viewable only until `trace_retention`
  expires, after which they MUST be evicted.

---

## 8. Data model & storage

### 8.1 Per-request trace context

- FR-CTX-1: The trace context MUST be allocated in `r->pool` and MUST hold the
  ordered step list, deltas, upstream tries, bodies, and fault for the request.
- FR-CTX-2: Steps MUST be append-only, ordered by `seq` then `t_offset_us`.
- FR-CTX-3: The context MUST be retrievable via the module's per-request context
  slot (`ngx_http_get_module_ctx`).

### 8.2 Shared-memory session store

- FR-SHM-1: A single shared-memory zone (`trace_zone`) MUST hold both active
  sessions and the bounded transaction ring buffer, allocated via nginx's slab
  allocator.
- FR-SHM-2: All access to the shared zone MUST be guarded by the zone's mutex;
  hold time MUST be minimized (copy out, then release).
- FR-SHM-3: On reload, an existing zone with the same name+tag MUST be reused and
  stale/expired sessions reconciled (not duplicated).
- FR-SHM-4: The stored transaction representation MUST be serializable to the
  JSON schema in §8.3 on read by any worker.

### 8.3 JSON schema (normative shapes)

The API MUST emit objects conforming to the shapes below (field names normative;
IDEA §6 shows full examples).

- **TraceSession**: `id`, `created_at`, `active_since`, `expires_at`, `state`
  (`capturing`|`stopped`|`expired`), `stopped_reason`
  (`null`|`expired`|`max_reached`|`manual`), `max_transactions`, `captured`,
  `filter` (`path_prefix`, `fault_only`, `fault_code`).
- **TransactionSummary** (list tier): `seq`, `method`, `uri`, `status`,
  `started_at`, `ended_at`, `duration_us`, `fault` (bool). Note the field names:
  `uri` (not `path`), `status` (not `final_status`), and `seq` identifies the
  transaction within its session — there is no separate `id`, `session_id`,
  `client_addr`, `upstream`, or `fault_code` in this tier.
- **Transaction** (detail tier): `txn`, `method`, `uri`, `worker_pid`,
  `connection_id`, `status`, `steps[]`, and — when body capture is enabled —
  `request_body` and `response_body`.
- **Step**: `seq`, `phase`, `handler`, `t_offset_us`, `status`, optional `type`
  (`condition`|`subrequest`), `evaluated` (for condition steps), `duration_us`
  (when the step was timed by Layer 2), `note`, and `vars` (map of
  `{value, op}`).
- **Body** (`request_body` / `response_body`): `captured_bytes`, `total_bytes`,
  `truncated`, and optionally `content_type`, `content_encoding`, and exactly one
  of `preview` (UTF-8 text) or `preview_hex` (binary).
- **Fault**: `phase`, `handler`, `code`, `status`, `error_state`, `message`,
  `step_seq`.

- FR-JSON-1: `vars[].op` MUST be one of `read`/`set`/`set_failed`.
- FR-JSON-2: `TransactionSummary` MUST NOT include heavy detail (no `steps`, no
  bodies, no upstream tries).
- FR-JSON-3: gRPC upstream tries MUST use the gRPC shape (`protocol: "grpc"`,
  `grpc_status`, `grpc_message`, `trailers`).
- FR-JSON-4: `total_bytes` MUST report the true body size even when fewer bytes
  were captured, so an operator can tell a capped capture from a short body.
- FR-JSON-5: A body preview MUST be emitted as `preview_hex` when the captured
  bytes are not valid UTF-8, and as `preview` otherwise; never both.

---

## 9. Control-plane API

Served only from a location carrying the `trace_control` directive; every endpoint
MUST require auth (enforced by nginx config, e.g. ACL + `auth_basic`) and the
module MUST NOT serve these endpoints elsewhere. Paths below are written against
the conventional `/__trace/` prefix, but the prefix is whatever location the
directive is placed on — routing is relative to it.

| ID | Method & path | Requirement |
|----|---------------|-------------|
| FR-API-1 | `POST /__trace/sessions` | Create a session. Parameters are supplied as **query arguments**, not a JSON request body: `max`, `path`, `fault_only`, `fault_code`. MUST return the new `TraceSession` with `201 Created`, and MUST reject with `429` when `trace_max_sessions` is reached. |
| FR-API-2 | `GET /__trace/sessions` | List sessions with `state`. |
| FR-API-3 | `GET /__trace/sessions/{id}` | Return `state`, `captured`, `active_since`, `expires_at`. |
| FR-API-4 | `DELETE /__trace/sessions/{id}` | Stop early; MUST set `stopped_reason: "manual"`. |
| FR-API-5 | `GET /__trace/sessions/{id}/transactions` | List tier: `TransactionSummary[]`. MUST be pollable while `state == capturing`. |
| FR-API-6 | `GET /__trace/sessions/{id}/transactions/{txn}` | Detail tier: full `Transaction`, addressed by the summary's `seq`. |
| FR-API-7 | `GET /__trace/sessions/{id}/export` | Return the whole session (summaries + all details) as one JSON artifact. |
| FR-API-8 | `GET /__trace/sessions/{id}/share` | Return a time-boxed shareable deep-link URL valid until retention expiry. |
| FR-API-9 | `POST /__trace/import` | Validate a previously exported session JSON and report what it contains. See FR-API-14. |
| FR-API-10 | `GET /__trace/ui[?session=&txn=]` | Serve the SPA; query params deep-link a session/transaction. |

- FR-API-11: Responses MUST be `application/json` (except the UI asset endpoints).
- FR-API-12: When `trace_zone` is unconfigured, all API endpoints MUST return
  `503`.
- FR-API-13: Unknown session/transaction ids MUST return `404`. A known route
  reached with the wrong method MUST return `405`.
- FR-API-14: `POST /__trace/import` MUST NOT re-inject the imported transactions
  into the shared ring. Accepting trace content from a POST would let a caller
  forge arbitrary records into an operator's live capture, so the endpoint
  validates the artifact's shape and returns a count; the offline viewer renders
  the file **client-side** (which is also what lets it work from `file://` with no
  nginx at all). It MUST reject a request whose body was buffered to disk rather
  than read it back, preserving G8 on the control plane.
- FR-API-15: Every captured byte rendered by the UI MUST be HTML-escaped before
  insertion into the document. Captured payloads are attacker-influenced, so a
  viewer that renders them raw converts each inspected request into stored XSS
  against the operator.

---

## 10. User interface requirements

- FR-UI-1: The UI MUST be a static SPA served by nginx that consumes only the §9
  API.
- FR-UI-2: The UI MUST show a left-rail list of captured transactions (method,
  path, status, duration, fault badge) and MUST grow it in near-real-time while a
  session is `capturing`.
- FR-UI-3: The UI MUST render a center timeline of phase/steps with
  expand/collapse of flow groups (grouped by scope), a per-step status icon
  (`success`/`error`/`skipped`/`disabled`), and per-step elapsed time with an "ε"
  marker for sub-millisecond steps.
- FR-UI-4: The right panel MUST show, for the selected step: variable
  snapshot/diff with `read`/`=`/`≠` markers, request/response headers, Properties
  (hidden by default), and upstream request/response detail (incl. gRPC trailers).
- FR-UI-5: The UI MUST provide per-transaction search that highlights matches and
  auto-expands collapsed groups.
- FR-UI-6: The UI MUST provide view options (show skipped/disabled/conditions/
  flow-info) persisted per user.
- FR-UI-7: The UI MUST provide an offline mode that opens an exported/imported
  session JSON without a live nginx, and a share action that copies the deep-link.

---

## 11. eBPF add-on interface (Layer 4) — OPTIONAL

Only the contract is normative here; the agent's implementation (Rust + `aya`) is
out of scope (CON-SCOPE-4).

- FR-EBPF-1: The eBPF agent MUST emit records conforming to the §8.3 step/
  transaction schema, tagged `source: "ebpf"`.
- FR-EBPF-2: Records MUST be correlatable to in-process transactions by
  `{worker_pid, connection_id, timestamp}`.
- FR-EBPF-3: The agent MUST be off by default and MUST require explicit enablement
  (`trace_ebpf on`) plus elevated privilege; TLS plaintext capture MUST require
  the additional `tls` argument.
- CON-EBPF-1: The C core MUST function fully with the agent absent
  (CON-ARCH-4).

---

## 12. Security & privacy requirements

- NFR-SEC-2: Redaction MUST be applied to configured header/variable names and to
  bodies **before** any bytes are written to the shared-memory zone.
- NFR-SEC-3: The default redaction set MUST include `authorization`, `cookie`,
  and `set-cookie`.
- NFR-SEC-4: Body capture MUST be off by default (client and upstream).
- NFR-SEC-5: The control-plane location MUST be usable only behind
  nginx-enforced access control; the module MUST NOT expose trace data on any
  location without the `trace_control` directive.
- NFR-SEC-6: Session TTL (`expires_at`) MUST be enforced so tracing cannot remain
  on indefinitely. A session with a zero/absent TTL MUST fall back to
  `trace_retention`, and a resolved TTL of zero MUST be floored at 60 s rather
  than treated as "never expires".
- NFR-SEC-7: A hardened-mode switch (`trace_hardened`, FR-CFG-21) MUST be able to
  disable body capture entirely, overriding every per-location setting.
- NFR-SEC-8: gRPC metadata and payloads MUST be subject to the same redaction and
  opt-in decoding rules as other data.
- NFR-SEC-9: The redaction mask MUST be fixed-width and content-free (the literal
  `[REDACTED]`). A length-preserving mask MUST NOT be used, because a preserved
  length leaks the size of the secret.
- NFR-SEC-10: Captured payload bytes MUST NOT be written to the error log at any
  level when `trace_hardened` is on.

---

## 13. Non-functional requirements

- NFR-PORT-1: The core module MUST be implementable as a dynamic module
  (`load_module`) and MUST declare a minimum supported nginx version; `PRECONTENT`
  usage MUST be conditional on version support.
- NFR-PORT-2: The module MUST be written in C (per IDEA §5.6 decision).
- NFR-REL-1: A wrapped handler or upstream callback MUST never change the
  request's outcome vs. an untraced request (behavioral transparency).
- NFR-REL-2: Any capture failure (allocation failure, cap exceeded, malformed
  data) MUST degrade gracefully (skip the datum, mark truncated/partial) and MUST
  NOT crash the worker or finalize the request abnormally.
- NFR-MEM-1: All trace memory MUST be either request-pool (auto-freed) or slab
  (bounded); there MUST be no unbounded growth.
- NFR-OBS-1: The module MUST emit error-log entries for session lifecycle,
  version-gate refusals (Layer 2), and capture-degradation events.

### 13.1 Self-diagnostics logging

This section specifies the module's **own** operational logging (how `ngx-trace`
logs *its own processing*). This is distinct from the trace data it captures
(§8): self-diagnostics describe the module's internal behavior for
troubleshooting the module itself, never the traced traffic's payloads.

- FR-LOG-1: The module MUST support a self-diagnostics log controlled by
  `trace_log` (FR-CFG-15). When `off`, diagnostics MUST still be emitted to
  nginx's standard `error_log` at the configured level; when a path is given,
  diagnostics MUST additionally be written to that dedicated file.
- FR-LOG-2: Verbosity MUST be governed by `trace_log_level` (FR-CFG-16) with the
  ordered levels `off < error < warn < info < debug < trace`. A message MUST be
  emitted only if its severity is at or above the configured level.
- FR-LOG-3: Each self-diagnostics entry MUST include a timestamp, worker PID,
  and level. Where applicable, diagnostics SHOULD include `session_id` and
  `txn_id` for correlation with captured data and across workers.
- FR-LOG-4: At `info` and above, the module SHOULD log lifecycle events: module
  init/exit, shm zone init, session create/stop/expire, and capture-degradation
  events (allocation failure, cap exceeded, Layer-2 version-gate refusal).
- FR-LOG-5: At `debug` and `trace`, the module MAY log per-request processing —
  selection decision (traced/skipped + reason), per-phase step boundaries,
  handler-wrap enter/exit with return codes, upstream send/receive milestones,
  and commit-at-`LOG` — to reconstruct the module's own decision flow. `trace`
  MAY include byte counts and offsets but MUST NOT include captured payload
  bytes.
- FR-LOG-6: Self-diagnostics logging MUST be subject to the same redaction
  guarantees as capture (§12): header values, variable values, and body bytes
  MUST NOT appear in diagnostics unless already permitted by `trace_redact`
  policy; secrets MUST NEVER be logged regardless of level.
- NFR-LOG-1: When `trace_log_level` is `off` (or below the message severity),
  diagnostics emission MUST add no measurable per-request overhead (the level
  check MUST short-circuit before formatting).
- NFR-LOG-2: Self-diagnostics logging MUST be non-blocking and MUST NOT stall the
  worker event loop; a failure to write diagnostics MUST NOT affect request
  processing or trace capture.
- NFR-LOG-3: The diagnostics log MUST be safe under multi-worker operation
  (writes from concurrent workers MUST NOT interleave within a single entry).

---

## 14. Constraints & assumptions

- CON-1: `FIND_CONFIG`, `POST_REWRITE`, `POST_ACCESS` do not accept custom
  handlers; their effects are inferred (CON-ARCH-1).
- CON-2: Layer-2 pointer wrapping depends on internal nginx table layouts and is
  version-gated (CON-L2-1).
- CON-3: eBPF (Layer 4) is Linux-only, requires BTF and `CAP_BPF`/root, and is
  out of the core's dependency graph (CON-ARCH-4, CON-EBPF-1).
- CON-4: gRPC/HTTP2 and non-buffered/streaming responses require special tap
  logic and firm body caps.
- CON-5: The `stream` (L4) module and UI-driven mutation are out of scope for v1.

---

## 15. Acceptance criteria (testable)

Each maps to requirements above and is verifiable in CI or manual test.

| ID | Acceptance criterion | Verifies |
|----|----------------------|----------|
| AC-1 | With no active session, a benchmark shows no statistically significant latency delta vs. the module unloaded. | NFR-PERF-1, FR-SEL-2 |
| AC-2 | Starting a session via `POST /__trace/sessions` (no reload), sending a request matching the filter, then `GET …/transactions` shows a summary row, and `GET …/transactions/{txn}` returns the full phase timeline within seconds. | FR-CORE-5, FR-API-1/5/6, §3.4 |
| AC-3 | A `proxy_pass` transaction's detail contains the exact request line + headers sent and the exact response status line + headers received, byte-for-byte (modulo redaction). | FR-UP-1..4 |
| AC-4 | A request that retries (first backend 502 → second 200) yields two `upstream.tries[]` entries. | FR-RETRY-1 |
| AC-5 | A failing gRPC call reports `grpc_status != 0` from trailers even though `:status == 200`. | FR-GRPC-2 |
| AC-6 | With `trace_body_capture both`, a JSON POST shows capped `request_body` and `response_body` previews with correct `truncated` flags. | FR-BODY-1/4/5 |
| AC-7 | A watched read-only variable that a `set` targets is recorded with `op: "set_failed"`. | FR-VAR-2 |
| AC-8 | An `if`/`try_files` branch not taken appears as a `type: "condition"` step with `status: "skipped"`. | FR-STATUS-2 |
| AC-9 | A 401 from `auth_request` populates `summary.fault` with the phase, handler, status, and linking `step_seq`. | FR-FAULT-1 |
| AC-10 | A `fault_only` session captures only failing requests; successful ones are discarded (ring buffer count unchanged). | FR-SEL-4 |
| AC-11 | `Authorization` values never appear unredacted in stored transactions or exports. | NFR-SEC-2/3 |
| AC-12 | A transaction traced by worker A is retrievable via the API served by worker B. | FR-SHM-1..4 |
| AC-13 | With `trace_intercept on`, a step names the specific module handler that ran; with it `off`, capture still succeeds at phase level. | FR-L2-1/4 |
| AC-14 | `ngx_trace_step()` from a cooperating module/`njs` adds a named step; called on a non-traced request it is a no-op. | FR-L3-1/2 |
| AC-15 | After `trace_retention` elapses, a completed session is evicted and its API endpoints return `404`; exporting before eviction and re-importing reproduces it in the offline viewer. | NFR-PERF-5, FR-API-7/9 |
| AC-16 | A wrapped handler/callback returning `NGX_AGAIN`/`NGX_DONE` produces identical request behavior to the untraced case. | FR-L2-2, NFR-REL-1 |
| AC-17 | With `trace_log_level debug`, the diagnostics log records the selection decision, per-phase step boundaries, and commit-at-`LOG` for a traced request, each entry carrying PID + `session_id`/`txn_id`; with the level `off` no such entries appear and per-request overhead is unchanged. | FR-LOG-2/3/5, NFR-LOG-1 |
| AC-18 | Self-diagnostics never emit redacted header/variable/body bytes at any level, and a diagnostics write failure does not affect request processing or capture. | FR-LOG-6, NFR-LOG-2 |

---

## 16. Delivery mapping (informative)

The phased plan (IDEA §13) maps to requirement sets as follows.

| Phase | Requirement scope |
|-------|-------------------|
| Phase 0 (spike) | FR-PHASE-1, FR-UP-2/3, FR-SHM-1, NFR-PORT-2 (language decision) |
| Phase 1 (MVP, Layer 1) | FR-CFG-1..4/9..14/20, FR-PHASE-*, FR-STATUS-*, FR-VAR-*, FR-UP-*, FR-GRPC-*, FR-RETRY-1, FR-FAULT-*, FR-SEL-*, FR-API-1..7, minimal FR-UI-* |
| Phase 2 (Layer 2 + depth) | FR-CFG-5/21, FR-L2-*, FR-BODY-*, NFR-SEC-7/9/10, subrequest correlation, full FR-UI-* (search/view options/offline/share), FR-API-8/9/14/15 |
| Phase 3 (Layer 3) | FR-L3-*, external collector |
| Phase 4 (Layer 4) | FR-CFG-8, FR-EBPF-*, CON-EBPF-1 |

**Implementation status (2026-07-26):** Phases 0–2 are built and verified;
Phase 3 is partially built (M9: `ngx_trace_step()` emit API, `GET /session`,
`GET /last`); M10 delivered hardening (8 REVIEW.md bug fixes, quality gates).
Phases 3–4 remaining work is not started. Requirements listed above that are
specified but not yet implemented — and the places where this document was
changed to match the code rather than the reverse — are enumerated in
`IMPLEMENTATION_PLAN.md` §10.

---

## 17. Traceability summary

Every requirement in this specification derives from `IDEA.md`:

- §1 goals ← IDEA §4.1
- §4 directives ← IDEA §7
- §5 phase/attribution ← IDEA §5.2, §5.3, §3.2
- §6 upstream/body/fault ← IDEA §5.4, §5.4.1, §6
- §7 selection/perf ← IDEA §9
- §8 data model/storage ← IDEA §6, §5.1
- §9 API ← IDEA §8
- §10 UI ← IDEA §8
- §11 eBPF interface ← IDEA §5.5, §5.6, §5.3 (Layer 4)
- §12 security ← IDEA §10
- §13 non-functional ← IDEA §9, §5.6, §11
- §13.1 self-diagnostics logging ← spec-level addition (operator-requested;
  extends IDEA §9 observability with a `trace_log`/`trace_log_level` self-log)
- §15 acceptance ← IDEA §14 (success criteria) + §15 (Apigee cross-check)
