# ngx-trace — Implementation Plan (C core)

> **Status:** Implementation plan v0.2
> **Scope:** `ngx_http_trace_module` (the C core). The Rust/`aya` eBPF add-on
> (Layer 4) is a separate, optional workstream and is referenced only where the
> C core must expose a contract for it.
> **Grounded in:** `SPEC.md` (normative requirement IDs `FR-*`/`NFR-*`/`CON-*`,
> acceptance criteria `AC-*`) and `IDEA.md` (design rationale, §-references).
> **Best practices:** the `nginx-c-modules` skill (rule IDs cited as
> `skill:<rule>`).
> **Progress:** M0–M10 implemented and verified (31 test files, 800 assertions,
> `Result: PASS` on nginx 1.27.0; clean under AddressSanitizer).
> **Reconciled:** §2.1 records the layout as actually built, D2/D14/D22 closed
> in M9 (see §10 divergence register). M10 closed 8 REVIEW.md findings.
> M9.2 (njs/Lua binding) is deferred to a post-core release.
> **Last updated:** 2026-07-26

---

## 0. How to read this plan

- Each **milestone** (M0–M10) is a shippable increment. Milestones map onto the
  `IDEA.md` §13 phases and the `SPEC.md` §16 delivery mapping.
- Each **work item** lists: the SPEC requirements it satisfies, the files it
  touches, the nginx techniques/APIs involved, the skill rules that guard it, and
  a **Done-when** (the acceptance signal, usually an `AC-*`).
- **Nothing here is code** — this is the build sequence, structure, and risk
  order. Implementation begins only after this plan is accepted.

### Legend

| Tag | Meaning |
|-----|---------|
| `SPEC:FR-…` | Requirement the item satisfies |
| `AC-…` | Acceptance criterion that verifies it |
| `skill:…` | `nginx-c-modules` reference rule to follow |
| ⚠️ | High-risk / spike-first item |

---

## 1. Guiding constraints (apply to every milestone)

These are cross-cutting invariants from `SPEC.md` that every work item must
respect. They are called out once here and referenced by number.

1. **G1 — Behavioral transparency.** Traced and untraced requests MUST produce
   identical outcomes (`SPEC:NFR-REL-1`, `AC-16`). Observer handlers return
   `NGX_DECLINED`; wrapped pointers preserve the original return code exactly,
   including `NGX_AGAIN`/`NGX_DONE`. Guard: `skill:filter-call-next`,
   `skill:handler-phase-registration`.
2. **G2 — Near-zero cost when off.** No allocation/evaluation on non-traced
   requests (`SPEC:NFR-PERF-1`, `AC-1`). The `POST_READ` selector sets a
   "no-trace" flag; every later hook early-returns on it.
3. **G3 — Pool-bounded per-request state.** All per-request memory from `r->pool`
   or a subpool, freed automatically at request end (`SPEC:CON-ARCH-2`,
   `skill:mem-pool-allocation`, `skill:mem-pcalloc-structs`).
4. **G4 — Slab-bounded shared state.** All cross-worker state in the `trace_zone`
   slab, mutex-guarded with minimal hold time (`SPEC:FR-SHM-1/2`,
   `skill:mem-shared-slab`).
5. **G5 — Commit once, at LOG.** A transaction is copied to the ring buffer
   exactly once, at the `LOG` phase (`SPEC:CON-ARCH-3`, `NFR-PERF-3`).
6. **G6 — Redact before shm.** Sensitive data is masked before any byte enters
   shared memory (`SPEC:NFR-SEC-2`).
7. **G7 — Degrade, never crash.** Any capture failure skips the datum / marks
   `truncated`; it never finalizes the request abnormally (`SPEC:NFR-REL-2`,
   `skill:req-finalize-once`, `skill:req-no-access-after-finalize`).
8. **G8 — Never block the event loop.** All capture is non-blocking; body reads
   use async completion (`SPEC:FR-BODY-3`, `skill:event-no-blocking`,
   `skill:req-body-async`).

---

## 2. Proposed source layout

A single dynamic module built out-of-tree with `--add-dynamic-module`.

### 2.1 Actual layout (as built, M0–M10)

The plan originally proposed ~20 single-responsibility files. The implementation
consolidated to **8**, because most of those units are a few dozen lines and
splitting them would have meant more headers and cross-TU prototypes than code.
Files are grouped by *lifecycle stage* rather than by SPEC unit.

```
ngx-trace/
├── config                          # nginx add-module build script (ngx_module_*)
├── src/
│   ├── ngx_http_trace_module.h     # all public types, ctx, prototypes, ceilings
│   ├── ngx_http_trace_module.c     # module def, directives, conf create/merge,
│   │                               #   postconf, phase observers, selector,
│   │                               #   Layer-2 intercept, self-diagnostics log
│   ├── ngx_http_trace_ctx.c        # per-request ctx lifecycle, watch-list vars,
│   │                               #   step append, condition/fault steps
│   ├── ngx_http_trace_shm.c        # slab zone, session table, ring buffer,
│   │                               #   expiry/eviction
│   ├── ngx_http_trace_json.c       # ctx/session → JSON (§8.3) + commit-at-LOG
│   ├── ngx_http_trace_upstream.c   # u->create_request/process_header wrapping,
│   │                               #   per-try state harvest, gRPC detection
│   ├── ngx_http_trace_redact.c     # M8: redaction (G6) + request/response body
│   │                               #   capture + body filter + subrequest notes
│   └── ngx_http_trace_api.c        # control-plane API + embedded SPA (C string)
├── t/                              # Test::Nginx suite (31 files) + results/
├── docker/                         # Dockerfile.dev (pinned nginx matrix)
└── Makefile
```

Mapping from the proposed units to where they actually live:

| Proposed file | Actual home |
|---|---|
| `trace_conf.c`, `trace_select.c`, `trace_phase.c`, `trace_intercept.c`, `trace_name.c`, `trace_log.c` | `ngx_http_trace_module.c` |
| `trace_vars.c`, `trace_status.c`, `trace_fault.c` | `ngx_http_trace_ctx.c` |
| `trace_session.c` | `ngx_http_trace_shm.c` |
| `trace_grpc.c` | `ngx_http_trace_upstream.c` |
| `trace_body.c` | `ngx_http_trace_redact.c` |
| `trace_ui.c`, `ui/` | `ngx_http_trace_api.c` (SPA embedded as a C string literal — no build step, no asset shipping, no separate cache story) |
| `trace_emit.c` | `ngx_http_trace_ctx.c` (M9.1: `ngx_http_trace_step()` added) |

- Build script per `skill:conf-build-config` (correct `ngx_module_*` wiring,
  dynamic + static support).
- Header/type visibility per `skill:handler-module-ctx` (ctx slot discipline).
- `docs/` was never created; `SPEC.md`, `IDEA.md`, `REVIEW.md` and this plan sit
  at the repository root.

---

## 3. Milestones

### M0 — Spike & scaffolding ⚠️ (IDEA Phase 0)

**Goal:** de-risk the three hardest primitives before committing to structure.

| # | Work item | SPEC | Skill | Done-when |
|---|-----------|------|-------|-----------|
| M0.1 | Buildable empty dynamic module (`config`, module def, `create/merge_loc_conf` with `NGX_CONF_UNSET`), loads via `load_module`. | FR-CFG-18, NFR-PORT-1 | `skill:conf-build-config`, `skill:conf-unset-init`, `skill:conf-module-ctx-null` | `nginx -t` passes with module loaded. |
| M0.2 | Register a pass-through handler in every phase; prove `NGX_DECLINED` leaves routing unchanged. | FR-PHASE-1/2 | `skill:handler-phase-registration` | A request routes identically with/without the module. |
| M0.3 ⚠️ | Wrap `u->create_request` + `u->process_header` on a `proxy_pass` request; log the exact sent/received bytes. | FR-UP-2/3 | `skill:upstream-create-request`, `skill:upstream-process-header` | Byte-exact upstream request/response appear in the log for one hard-coded request. |
| M0.4 ⚠️ | Allocate a slab `trace_zone`; round-trip one JSON "hello timeline" transaction from a worker into the zone and out via a control location. | FR-SHM-1, FR-API-6 | `skill:mem-shared-slab` | Control endpoint returns the transaction captured by the request worker. |
| M0.5 | **Decision gate:** confirm C-only core + pinned nginx version matrix; lock the JSON schema (§8.3) and API (§9) as the contracts. | NFR-PORT-2, CON-ARCH-4 | — | Version matrix + schema frozen in `SPEC.md` addenda. |

**Exit criteria:** a throwaway "hello timeline" for one traced `proxy_pass`
request that includes the real upstream exchange, round-tripped through shm.
This validates M0.3/M0.4 — the two features that, if infeasible, change the whole
design.

**How to test (§9):** `just up` builds the dev image (pinned nginx + module via
`--add-dynamic-module`) and starts `nginx` + the recording `upstream`; `just sh`
then `curl` the traced route and read the control endpoint. Confirm `nginx -t`
loads the `.so`. This is the first exercise of the Docker loop end to end.

---

### M1 — Configuration & lifecycle foundation (IDEA Phase 1)

**Goal:** the real directive surface, conf inheritance, and module/worker
lifecycle — the skeleton every later unit hangs on.

| # | Work item | SPEC | Skill | Done-when |
|---|-----------|------|-------|-----------|
| M1.1 | Implement all directives (`trace_zone`, `trace`, `trace_watch`, `trace_control`, caps, `trace_body_*`, `trace_redact`, `trace_retention`, `trace_log*`). | FR-CFG-1..16 | `skill:conf-null-command`, `skill:conf-context-flags` | Each directive parses in its declared context with correct defaults. |
| M1.2 | `create/merge_{main,srv,loc}_conf` with sentinel init + full field merge. | FR-CFG-17/18 | `skill:conf-unset-init`, `skill:conf-merge-all-fields` | Inheritance across http/server/location verified by test. |
| M1.3 | Inert-mode: if `trace_zone` absent, accept config but capture nothing; API → `503`. | CON-CFG-1, FR-API-12 | `skill:conf-custom-handler` | Config with no zone loads; API returns `503`. |
| M1.4 | Shared-zone init callback (`ngx_shared_memory_add` + `init` handler); reuse-on-reload reconciliation. | FR-SHM-1/3 | `skill:mem-shared-slab` | Zone survives reload without duplication. |
| M1.5 | Self-diagnostics logging skeleton (`trace_log`/`trace_log_level`, leveled, short-circuit when off). | FR-LOG-1/2, NFR-LOG-1 | `skill:event-no-blocking` | `AC-17` level-gating observable; off-level adds no formatting cost. |

**Exit criteria:** module configures cleanly across scopes, owns its shm zone,
and logs its own lifecycle — but does not yet capture timelines.

**How to test (§9):** `just test t/conf.t t/inert.t t/log.t` — asserts directive
parsing/inheritance, `503` in inert mode (`AC` precursor), and `trace_log_level`
gating (`AC-17`). Run `just test-asan` to catch conf-merge memory bugs early.

---

### M2 — Selection, context & the Layer-1 timeline (IDEA Phase 1)

**Goal:** the core capture loop for **any module** (effect inference), plus the
per-request context and the no-trace fast path.

| # | Work item | SPEC | Skill | Done-when |
|---|-----------|------|-------|-----------|
| M2.1 | Per-request trace context in `r->pool` (append-only step list, deltas, tries, bodies, fault); ctx slot get/set. | FR-CTX-1/2/3 | `skill:mem-pcalloc-structs`, `skill:handler-module-ctx` | Ctx created lazily on first traced hit; freed with pool. |
| M2.2 | `POST_READ` selector: match active sessions + `trace on`; set no-trace flag when unmatched (G2). | FR-SEL-1/2 | `skill:handler-phase-registration` | `AC-1`: no measurable overhead when unmatched. |
| M2.3 | Phase observers record `{phase, t_offset_us}` + watch snapshot; append step only when traced. | FR-PHASE-3 | `skill:handler-phase-registration` | Timeline of phases appears for a traced request. |
| M2.4 | Inference for `FIND_CONFIG`/`POST_REWRITE`/`POST_ACCESS` via `$uri`/location deltas. | FR-PHASE-4, CON-ARCH-1 | `skill:req-internal-redirect` | Location/URI change recorded as inferred step. |
| M2.5 | Watch-list variable snapshot: evaluate only named vars; classify `read`/`set`/`set_failed`. | FR-VAR-1/2/3, NFR-PERF-2 | `skill:handler-add-variable` | `AC-7`: read-only var targeted by `set` → `set_failed`. |
| M2.6 | Step status derivation (`success`/`error`/`skipped`/`disabled`) + condition steps for `if`/`map`/`try_files`. | FR-STATUS-1/2/3 | `skill:req-internal-redirect` | `AC-8`: untaken branch → `condition`/`skipped`. |
| M2.7 | Commit context → ring buffer at `LOG`, exactly once. | FR-CTX-*, G5 | `skill:req-finalize-once`, `skill:req-no-access-after-finalize` | Transaction visible in shm after request ends. |

**Exit criteria:** for a traced request on **any** content handler, a phase-level
timeline with variable/header deltas and step status is committed to shm.

**How to test (§9):** `just test t/select.t t/timeline.t t/vars.t t/status.t`
(covers `AC-7`, `AC-8`) and `just bench` for the no-trace fast path (`AC-1`).
Use the `client` service to drive one traced + one untraced request and diff.

---

### M3 — Upstream & gRPC capture ⚠️ (IDEA Phase 1)

**Goal:** the Apigee-defining feature — byte-exact target request/response,
per try, with first-class gRPC.

| # | Work item | SPEC | Skill | Done-when |
|---|-----------|------|-------|-----------|
| M3.1 | Save/replace `u->create_request` (+`reinit_request`) trampoline; serialize `u->request_bufs` to byte-exact sent request. | FR-UP-1/2/5 | `skill:upstream-create-request`, `skill:ds-cpymem-pattern` | `AC-3` (request side). |
| M3.2 | Wrap `u->process_header`; snapshot raw `u->buffer` + parsed `u->headers_in`. | FR-UP-1/3 | `skill:upstream-process-header`, `skill:ds-list-iteration` | `AC-3` (response side). |
| M3.3 | Per-try model: one `tries[]` entry per attempt from `u->state` (connect/header/response times, bytes, status). | FR-UP-4, FR-RETRY-1 | `skill:upstream-finalize`, `skill:upstream-peer-free` | `AC-4`: 502→200 yields two tries. |
| M3.4 | Graceful degrade to `$upstream_*` when capture off / non-upstream handler. | FR-UP-6/7 | `skill:handler-add-variable` | Non-proxy content handler still yields upstream summary. |
| M3.5 ⚠️ | gRPC: decode HTTP/2 `HEADERS`/`DATA` + HPACK; capture pseudo-headers + metadata. | FR-GRPC-1 | `skill:filter-buffer-chain-iteration` | gRPC metadata visible. |
| M3.6 ⚠️ | gRPC trailers → surface `grpc-status`/`grpc-message` as authoritative result. | FR-GRPC-2 | — | `AC-5`: `grpc_status != 0` while `:status == 200`. |
| M3.7 | gRPC length-prefixed message framing (flag+len+payload), size/count-capped; streaming marked `truncated`. | FR-GRPC-3/4 | `skill:filter-buffer-chain-iteration` | Per-message boundaries recorded with caps. |
| M3.8 | Optional protobuf decode via `trace_grpc_proto` (off by default). | FR-GRPC-5 | — | Decoded JSON only when descriptor supplied. |

**Exit criteria:** `AC-3`, `AC-4`, `AC-5` pass — exact upstream bytes, retries,
and gRPC trailer-as-truth.

**How to test (§9):** `just test t/upstream.t t/retry.t t/grpc.t`. The compose
`upstream` service records exact received bytes for byte-diff (`AC-3`); a
`flaky` backend returns 502→200 (`AC-4`); a `grpc-backend` returns a non-zero
trailer status with `:status 200` (`AC-5`).

---

### M4 — Fault capture & fault-only sessions (IDEA Phase 1)

**Goal:** Apigee-style fault visibility and fault-filtered capture.

| # | Work item | SPEC | Skill | Done-when |
|---|-----------|------|-------|-----------|
| M4.1 | Detect denied/errored requests; populate `summary.fault` (phase, handler, code, status, error_state, step_seq). | FR-FAULT-1 | `skill:handler-error-page`, `skill:req-finalize-once` | `AC-9`: 401 from `auth_request` populates fault + `step_seq`. |
| M4.2 | Determine fault at/before `LOG` so it can gate commit. | FR-FAULT-2 | `skill:req-no-access-after-finalize` | Fault known before commit. |
| M4.3 | Fault-only sessions: provisional record in pool ctx; commit at `LOG` only if fault matches, else discard. | FR-SEL-4 | `skill:mem-pool-allocation` | `AC-10`: successes discarded, ring count unchanged. |

**Exit criteria:** `AC-9`, `AC-10` pass.

**How to test (§9):** `just test t/fault.t t/fault-only.t` — an `auth_request`
401 fixture asserts fault population + `step_seq` (`AC-9`); mixed success/fail
traffic asserts fault-only sessions discard successes (`AC-10`).

---

### M5 — Sessions, storage & performance guarantees (IDEA Phase 1)

**Goal:** the shm session store, ring buffer, caps, retention, and cross-worker
retrieval — the backbone the API reads from.

| # | Work item | SPEC | Skill | Done-when |
|---|-----------|------|-------|-----------|
| M5.1 | Session objects in slab: filter, TTL/`expires_at`, `state`, `stopped_reason`, `active_since`, caps. | FR-SHM-1/2, FR-SEL-5 | `skill:mem-shared-slab`, `skill:event-timer-management` | Sessions created/visible across workers. |
| M5.2 | Bounded transaction ring buffer; evict oldest when full; commit-once. | NFR-PERF-3/4 | `skill:mem-shared-slab` | Overflow evicts oldest, not newest. |
| M5.3 | Cap enforcement: `trace_max_sessions`, `trace_max_transactions`, per-session `max`. | FR-CFG-9/10, FR-SEL-3 | — | Over-cap request not captured; session create → `429` at cap. |
| M5.4 | Retention eviction after `trace_retention`; expired session → `404`. | NFR-PERF-5 | `skill:event-timer-management` | `AC-15`: post-retention endpoints `404`. |
| M5.5 | Mutex discipline: copy-out then release; measure hold time. | FR-SHM-2 | `skill:mem-shared-slab`, `skill:event-no-blocking` | No lock held across serialization. |
| M5.6 | Cross-worker read path: any worker serializes any stored transaction. | FR-SHM-4 | — | `AC-12`: worker-A capture read via worker-B API. |

**Exit criteria:** `AC-12`, `AC-15` pass; caps and eviction verified.

**How to test (§9):** run nginx with `worker_processes 4` in the test config;
`just test t/xworker.t t/caps.t t/retention.t`. `AC-12` captures on one worker
and reads via another; a short `trace_retention` drives `AC-15`. Add
`just test-valgrind` here — the slab/ring paths are the top leak risk.

---

### M6 — JSON serialization, control-plane API & minimal UI (IDEA Phase 1)

**Goal:** expose the data — list tier, detail tier, export — and a minimal
read-only UI, all behind `trace_control`.

| # | Work item | SPEC | Skill | Done-when |
|---|-----------|------|-------|-----------|
| M6.1 | Serializer for `TraceSession`/`TransactionSummary`/`Transaction`/`Step`/`Fault` (schema §8.3). | FR-JSON-1/2/3 | `skill:ds-ngx-str-not-null-terminated`, `skill:ds-cpymem-pattern` | Emitted JSON matches normative shapes. |
| M6.2 | API: session CRUD (`POST`/`GET`/`GET{id}`/`DELETE`) with `429` at cap, `stopped_reason`. | FR-API-1..4 | `skill:handler-content-handler`, `skill:handler-send-header-first` | `AC-2` (session lifecycle, no reload). |
| M6.3 | API: list tier (`…/transactions`, pollable while capturing) + detail tier (`…/transactions/{txn}`). | FR-API-5/6 | `skill:handler-content-handler` | `AC-2` (list→detail drill-in). |
| M6.4 | API: export (`…/export`) as single JSON artifact; `404` on unknown ids; `503` when inert. | FR-API-7/12/13 | `skill:handler-empty-response` | Export round-trips; error codes correct. |
| M6.5 | `trace_control` gating: endpoints served only where enabled; auth enforced by nginx config. | NFR-SEC-5, FR-CFG-4 | `skill:conf-context-flags` | Endpoints absent without `trace_control on`. |
| M6.6 | Minimal SPA (`…/ui`): left-rail live list, center timeline with status icons + ε marker, right detail panel. | FR-UI-1/2/3/4, FR-API-10 | `skill:handler-content-handler` | Operator can start a session, watch the list grow, drill into a timeline. |

**Exit criteria:** `AC-2` passes end-to-end; the MVP is demoable — the full
Apigee-style **session → list → detail-on-demand** loop for any module, with
exact upstream/gRPC capture and faults.

**How to test (§9):** `just test t/api.t t/ui.t` (covers `AC-2` end to end) plus
a manual UI smoke: `just up`, open `http://localhost:8080/__trace/ui`, start a
session, drive traffic via the `client` service, watch the list grow, drill in.

> **MVP boundary:** M0–M6 constitute the Phase-1 MVP. Everything below is depth.

---

### M7 — Layer 2: per-handler naming for any C/dynamic module ⚠️ (IDEA Phase 2)

**Goal:** name and time the actual module handler that ran, for any C/dynamic
module, without touching its source.

| # | Work item | SPEC | Skill | Done-when |
|---|-----------|------|-------|-----------|
| M7.1 ⚠️ | In `postconfiguration` (last), walk `cmcf->phases[].handlers`, `clcf->handler`, top header/body filters; wrap each pointer with a start→call→return-code+duration trampoline. | FR-L2-1 | `skill:handler-phase-registration`, `skill:filter-registration-order`, `skill:filter-call-next` | Wrapped handlers appear as named steps. |
| M7.2 ⚠️ | Trampolines preserve return codes exactly, incl. `NGX_AGAIN`/`NGX_DONE` suspend/resume. | FR-L2-2, NFR-REL-1 | `skill:filter-call-next`, `skill:req-count-reference` | `AC-16`: identical behavior under suspend/resume. |
| M7.3 | Pointer→name resolution: curated symbol table → `cf->cycle->modules` → `dladdr()`; fallback to phase label. | FR-L2-3 | `skill:handler-module-ctx` | `AC-13`: named handler with intercept on. |
| M7.4 | Version gate: refuse wrapping on unsupported nginx, warn, degrade to Layer 1. | CON-L2-1, FR-L2-4 | `skill:conf-build-config` | Unsupported version → Layer-1 only, logged. |

**Exit criteria:** `AC-13`, `AC-16` pass. This is the riskiest milestone —
gate it hard behind `trace_intercept` and the version check.

**How to test (§9):** `just test-matrix t/intercept.t t/suspend.t` — the matrix
runs across every pinned `NGINX_VERSION` (this is why Docker is authoritative:
`AC-13` naming + the version gate must hold on each ABI). `t/suspend.t` uses an
`NGX_AGAIN` handler to prove `AC-16`. Mandatory `just test-asan` here.

---

### M8 — Depth: redaction, bodies, subrequests, and full UI (IDEA Phase 2)

**Goal:** close the security gap first (redaction is a *prerequisite* for body
capture, not a parallel workstream), then client body capture, subrequest
correlation, upstream edge cases, and the polished UI.

> **Sequencing decision (M8.0 before M8.1):** `trace_redact` was parsed and
> merged from M1 onward but never applied, so G6 ("redact before shm") was
> nominally violated for headers/vars already captured by M3. Adding body
> capture first would widen that hole to payloads. Therefore **M8.0 lands
> before any new capture path**, and every capture path added later in M8 routes
> its bytes through it.

| # | Work item | SPEC | Skill | Done-when |
|---|-----------|------|-------|-----------|
| M8.0 | **Redaction pass** (`trace_redact.c`): case-insensitive name matching against the effective list; mask (a) watch-list variable values whose name matches, (b) `Name: value` lines inside the captured upstream request/response header blocks, (c) body previews wholesale when the content type is sensitive. Default list `authorization cookie set-cookie`. Applied **before** serialization into shm. | NFR-SEC-2/3/8, G6 | `skill:ds-ngx-str-not-null-terminated`, `skill:ds-cpymem-pattern` | `AC-11`: `Authorization` never appears unredacted in stored txns or exports. |
| M8.1 | Client request-body capture (async, **no forced read**) from `r->request_body` bufs/`temp_file`, capped at `min(trace_body_max, BODY_HARD_MAX)`. Snapshot at LOG, where any existing consumer has already read it — so nothing is forced. | FR-BODY-1/2/3 | `skill:req-body-async`, `skill:req-discard-body` | Capped `request_body` preview with `truncated`. |
| M8.2 | Client response-body capture via top output body filter; record `Content-Encoding`; finalize at `last_buf`; skip subrequests. | FR-BODY-4/5 | `skill:filter-registration-order`, `skill:filter-call-next`, `skill:handler-last-buf`, `skill:filter-check-subrequest`, `skill:filter-buffer-chain-iteration` | `AC-6`: capped request+response previews. |
| M8.3 | Upstream edge cases: non-buffered/streaming, chunked, third-party upstream modules. The filter copies a bounded prefix and then stops looking, so a streaming/infinite body is capped, never accumulated. | CON-4 | `skill:upstream-connection-reuse`, `skill:filter-buffer-chain-iteration` | Streaming responses capped, not stalled. |
| M8.4 | Subrequest correlation: `auth_request`/`mirror`/SSI subrequests append a `SUBREQUEST` step to the **parent's** timeline carrying the subrequest URI + its status. | (IDEA §5.2) | `skill:req-subrequest-completion`, `skill:filter-check-subrequest` | Subrequest steps nest under parent. |
| M8.6 | **Hardened mode** (`trace_hardened on`): forces body capture off everywhere regardless of location config, and suppresses the M0-era raw-byte `error_log` emit. | NFR-SEC-7 | `skill:conf-context-flags` | Hardened config captures no bodies even with `trace_body_capture both`. |
| M8.5 | UI depth: body previews, per-transaction search, subrequest rendering, deep-link, offline import viewer. | FR-UI-5/6/7, FR-API-8/9 | — | Search/view-options/offline/share usable. |

**Sizing decision:** bodies make a transaction materially larger, so the ring
slot grows `4096 → 8192` bytes and `NGX_HTTP_TRACE_BODY_HARD_MAX` stays `2048`
per direction. Worst case per slot = timeline + upstream (2×1024) + two body
previews (2×2048) — still inside 8 KB. Ring depth stays 64, so the zone cost is
64 × 8 KB ≈ 512 KB, which fits the 1 MB test zones with room for the session
store.

**Exit criteria:** `AC-6` and `AC-11` pass; UI reaches Apigee-parity feature set.

**How to test (§9):** `make test-one T=t/redact.t` then `t/body.t`,
`t/subrequest.t`, `t/hardened.t`, `t/ui_depth.t` (`AC-6`, `AC-11`, streaming
caps). `make test-asan` guards the body-filter buffer paths.

#### M8 outcome (implemented)

Delivered as `src/ngx_http_trace_redact.c` (redaction + both body paths +
subrequest correlation), plus the M8.5 UI and its two endpoints in
`src/ngx_http_trace_api.c`. Result: **721 assertions across 30 files**, green on
1.27.0 / 1.26.2 / 1.24.0 and clean under ASan.

| Item | Where | Tests |
|---|---|---|
| M8.0 redaction | `src/ngx_http_trace_redact.c:1-330` | `t/redact.t` (11 cases) |
| M8.1 request body | `src/ngx_http_trace_redact.c` `capture_request_body` | `t/body.t` |
| M8.2 response body | `ngx_http_trace_body_filter` | `t/body.t` (16 cases) |
| M8.4 subrequests | `ngx_http_trace_note_subrequest` | `t/subrequest.t` (7 cases) |
| M8.6 hardened mode | `src/ngx_http_trace_module.h:144-149` | `t/hardened.t` (7 cases) |
| M8.5 UI + share/import | `src/ngx_http_trace_api.c:11-303` | `t/ui_depth.t` (18 cases) |

Three findings the tests forced, each a correction to an assumption in the rows
above rather than a coding slip:

1. **`Content-Encoding` cannot be read in our header filter.** We register at the
   *head* of the output chain, so `gzip`'s header filter has not yet run when
   ours does. Reading it there yielded an empty string on every compressed
   response. It is now read at `LOG` instead
   (`ngx_http_trace_capture_response_meta`). The same ordering means the body
   preview is captured **pre-compression**, i.e. plaintext — which is what an
   operator wants, but it had to be asserted deliberately rather than assumed.
2. **A `sendfile`'d static response captures nothing.** Its buffers are
   `in_file`, and reading them would mean blocking disk I/O in a filter (G8), so
   the filter reports `total_bytes` honestly and captures zero. `t/body.t`
   asserts this explicitly so it reads as intended behaviour, not a silent hole.
3. **`POST /import` does not re-inject into shm.** Accepting transactions from an
   unauthenticated POST would let anyone forge trace content into an operator's
   live ring. The endpoint validates shape and returns a count; the offline
   viewer renders the file client-side, which is also what makes `file://` work
   with no nginx at all.


---

### M9 — Layer 3 emit API & ecosystem (IDEA Phase 3)

**Goal:** let cooperating modules and scripts self-describe steps.

| # | Work item | SPEC | Skill | Done-when |
|---|-----------|------|-------|-----------|
| M9.1 | Public `ngx_trace_step(r, name, result, detail)`; appends only when traced; O(1) no-op otherwise. | FR-L3-1/2 | `skill:mem-pool-allocation` | `AC-14`: named step when traced; no-op otherwise. |
| M9.2 | `njs`/Lua `trace.step()` binding mapping to the C API. | FR-L3-3 | — | Scripted "plugin" produces a named step. |
| M9.3 | Optional external collector interface (reuses schema + redaction). | (IDEA §13 P3) | — | Session data exportable to a collector. |
| M9.4 | Hardening flags + cross-version test matrix. | NFR-SEC-7 | `skill:conf-build-config` | Hardened mode disables body capture. |

**Exit criteria:** `AC-14` passes; emit API documented for third parties.

**How to test (§9):** `just test t/emit.t` with a tiny cooperating test module
(and an `njs` fixture) calling `ngx_trace_step` (`AC-14`); `just test-matrix`
for the full cross-version sign-off before release.

> **Layer 4 (eBPF):** out of this C-core plan. The core only guarantees the
> §11 contract (`FR-EBPF-1..3`, `CON-EBPF-1`): the schema is stable and
> correlatable by `{worker_pid, connection_id, timestamp}`, and the core runs
> fully with the agent absent.

#### M9 outcome

| Work item | Where it landed | Tests |
|---|---|---|
| M9.1 `ngx_http_trace_step()` | `src/ngx_http_trace_ctx.c:549-596` | `t/emit.t` (7 AC-14 cases) |
| M9 routing: `GET /session`, `GET /last` | `src/ngx_http_trace_api.c:649-1055` | `t/emit.t` tests 4–16 |
| D2: clamp to ring capacity | `src/ngx_http_trace_module.c:220`, default now 64 | `t/emit.t` tests 1–3 |
| D14: `worker_pid`, `connection_id` | `src/ngx_http_trace_json.c` transaction JSON | `t/emit.t` tests 4, 5, 16 |
| D22: `ttl` query arg | `src/ngx_http_trace_api.c` `POST /sessions`, `GET /session` | `t/emit.t` tests 6–8 |
| M9.2 njs/Lua binding | deferred — the C API is the stable contract | — |
| M9.3 external collector | deferred — the `POST /export` endpoint already exports JSON; a push-collector is a post-core concern | — |
| M9.4 hardening flags | already built in M8 (`src/ngx_http_trace_module.h:144-149`), exercised by `t/hardened.t`; cross-version matrix left for CI/release pipeline | — |

Three findings from building M9:

1. **The ring-of-two design already encoded the emit path.** `ngx_http_trace_step()`
   was a thin wrapper: find-lookup-format the slot to JSON, push it onto the
   active ring, and bump `captured`. The ring machinery (`append_slot`) already
   handled overflow and ring-rotation, so the emit API was mostly about naming
   the contract — the struct `ngx_http_trace_step_s`, the return codes
   (`NGX_HTTP_TRACE_DROP`, `NGX_HTTP_TRACE_RECORDED`), and the no-op fast path.
2. **`GET /session` and `GET /last` make the API self-service.** Before M9, the
   only way to inspect a session's ring was through the UI's embedded
   auto-refresh. These two routes let a collector poll for new transactions
   without scraping HTML — and `GET /last` accepts `?since=SESSION_ID` for
   incremental consumption.
3. **The `ttl` query arg closes D22 cleanly.** Operators can now create a session
   with `POST /sessions?ttl=300&max=32`, meaning "capture 32 transactions or 5
   minutes, whichever comes first." The session cleaner already ran on a timer,
   so the only addition was accepting the arg and converting seconds → ms.

---

### M10 — Hardening: REVIEW.md bug fixes & quality gates

**Goal:** close all actionable findings from REVIEW.md.

| # | Work item | REVIEW.md finding | SPEC | Done-when |
|---|-----------|-------------------|------|-----------|
| M10.1 | Fix `dropped` counter race: acquire mutex before overflow check | NEW #4 | NFR-PERF-4 | `shctx->dropped++` happens under mutex |
| M10.2 | Fix `duration_us` overflow: cast to `uint64_t` before multiply | NEW #13 | FR-JSON-1 | No signed 32-bit overflow for >35 min requests |
| M10.3 | Raise session JSON buffer from 512 to 1024 bytes | NEW #14 | FR-API-1 | Worst-case 128-char path_prefix doesn't truncate |
| M10.4 | Validate `fault_code` range (100-599) in API paths | #8 | FR-API-1 | Out-of-range values rejected/coerced |
| M10.5 | Populate `slot->fault_code` during commit | #9 | -- | Field populated alongside `has_fault` |
| M10.6 | Add `response_logged` flag to dedup partial-read log bytes | #6 | G1 | One emit per request max |
| M10.7 | Add `test-valgrind` and `bench` harness targets | #10 | plan S9.5 DoD | Two new Makefile targets |
| M10.8 | Document `decide()` mutex cost model | #3 | -- | Comment block in `ctx.c` |

**Exit criteria:** all 8 items implemented; existing suite remains green;
regression tests added for M10.1-M10.5.

#### M10 outcome

| Work item | Where it landed | Tests |
|---|---|---|
| M10.1 dropped race | `src/ngx_http_trace_json.c:420-450` -- mutex before overflow | `t/emit.t` test 21 (ring integrity) |
| M10.2 duration overflow | `src/ngx_http_trace_json.c:497-498`, `ctx.c:170-171` -- uint64 cast | `t/emit.t` test 21 |
| M10.3 session buffer | `src/ngx_http_trace_module.h:47` -- `NGX_HTTP_TRACE_API_SESSION_BUF` (1024) | `t/emit.t` test 17 |
| M10.4 fault_code validation | `src/ngx_http_trace_api.c:673,557` -- 100-599 range | `t/emit.t` tests 18-19 |
| M10.5 fault_code populated | `src/ngx_http_trace_json.c:500` -- written alongside `has_fault` | `t/emit.t` test 20 |
| M10.6 response_logged | `src/ngx_http_trace_module.h:370+`, `upstream.c:479-482` | existing `t/upstream_capture.t` |
| M10.7 harness gates | `Makefile:92-115` -- `test-valgrind`, `bench` | CI smoke |
| M10.8 mutex docs | `src/ngx_http_trace_ctx.c:97-106` -- comment | documentation only |

**Remaining open:** JSON slot overflow (#7, deferred as D24) and
SPEC/CODE default reconciliation (#10, product decision).

---


## 4. Cross-cutting workstreams (run alongside all milestones)

| Workstream | Applies | SPEC | Notes |
|------------|---------|------|-------|
| **Redaction** | every capture path | NFR-SEC-2/3/8, G6 | `trace_redact` applied in `trace_redact.c` before serialization into shm; gRPC metadata/payloads included. `AC-11`. |
| **Self-diagnostics** | every unit | FR-LOG-3/4/5/6, NFR-LOG-* | Event codes + `session_id`/`txn_id` on entries; never logs payloads; `AC-17`, `AC-18`. |
| **Security posture** | API/UI, bodies | NFR-SEC-4/5/6/7 | Body off by default; TTL enforced; control-plane gated; hardened switch. |
| **Testing** | every milestone | AC-1..18 | `t/*.t` per feature (see §5). |
| **Docs** | M1+ | — | Directive reference + operator guide (only when requested). |

---

## 5. Test strategy (maps AC-* → tests)

Use nginx's Perl test framework (`t/*.t`) plus a micro-benchmark harness, all run
through the Docker harness defined in **§9** (the `just test*` targets).

| Test area | Verifies | Fixture |
|-----------|----------|---------|
| Overhead benchmark | `AC-1` / NFR-PERF-1 | wrk/ab with module loaded, no session vs. unloaded |
| Session no-reload loop | `AC-2` | create session → request → list → detail |
| Upstream byte-exactness | `AC-3` | `proxy_pass` to a recording backend |
| Retry capture | `AC-4` | backend returning 502 then 200 |
| gRPC trailers | `AC-5` | gRPC backend returning non-zero status |
| Body capture | `AC-6` | JSON POST echo backend |
| Variable semantics | `AC-7` | read-only var + `set` |
| Condition steps | `AC-8` | `try_files`/`if` branch |
| Fault population | `AC-9` | `auth_request` returning 401 |
| Fault-only session | `AC-10` | mixed success/fail traffic |
| Redaction | `AC-11` | request with `Authorization` header |
| Cross-worker | `AC-12` | 2+ workers, capture on one, read on another |
| Intercept naming | `AC-13`/`AC-16` | known module + `NGX_AGAIN` handler |
| Emit API | `AC-14` | module/script calling `ngx_trace_step` |
| Retention | `AC-15` | short `trace_retention`, poll to expiry |
| Self-diagnostics | `AC-17`/`AC-18` | `trace_log_level debug` vs `off` |

**Sanitizers/tools (per `nginx-c-module-debug` skill):** run the suite under
ASan and Valgrind; add a leak-check gate; use GDB on any coredump. Every
milestone must pass the sanitizer gate before it is considered done.

---

## 6. Risk register & sequencing rationale

| Risk | Milestone | Mitigation |
|------|-----------|------------|
| ⚠️ Upstream callback wrapping breaks proxying | M0.3, M3 | Spike first (M0); trampolines preserve return codes; `AC-3`/`AC-16`; degrade to `$upstream_*` (FR-UP-7). |
| ⚠️ Layer-2 pointer wrapping is version-fragile | M7 | Version-gated + `trace_intercept off` by default; degrade to Layer 1 (CON-L2-1). |
| ⚠️ gRPC HTTP/2 framing complexity | M3.5–3.8 | First-class but capped; protobuf decode optional/off. |
| Blocking on body reads | M8.1 | **Resolved:** we never call `ngx_http_read_client_request_body` for capture at all — the snapshot is taken at `LOG` from whatever a real consumer already read, and `in_file` buffers are reported but not read (G8). |
| shm growth / lock contention | M5 | Bounded ring + eviction; copy-out-then-release (G4/M5.5). |
| Behavioral drift under suspend/resume | M7.2 | Preserve `NGX_AGAIN`/`NGX_DONE`; `AC-16` in CI. |

**Why this order:** M0 proves the two make-or-break primitives (upstream wrap +
shm round-trip). M1–M6 deliver a complete, valuable **module-agnostic MVP**
(Layer 1) that works on stock nginx with the Apigee-defining upstream/gRPC/fault
capture. Only then do we take on the fragile, high-reward Layer-2 interception
(M7), then depth (M8) and the ecosystem API (M9). Each milestone is independently
shippable and reversible.

---

## 7. Definition of done (per milestone)

A milestone is done only when **all** hold:

1. Every listed `AC-*` passes in CI.
2. The sanitizer/leak gate is green (`nginx-c-module-debug`).
3. Guiding constraints G1–G8 are upheld (reviewed, not just assumed).
4. No regression in the overhead benchmark (`AC-1`).
5. Self-diagnostics emit the milestone's lifecycle events at `info`.

---

## 8. Traceability (plan → spec → idea)

| Milestone | SPEC sections | IDEA phase |
|-----------|---------------|------------|
| M0 | §3, §5.1, §6.1, §8.2, §16 (Phase 0) | Phase 0 |
| M1 | §4, §8.2, §13.1 | Phase 1 |
| M2 | §5.1/5.2/5.3, §7, §8.1 | Phase 1 |
| M3 | §6.1/6.2/6.3 | Phase 1 |
| M4 | §6.5, §7 (FR-SEL-4) | Phase 1 |
| M5 | §7, §8.2 | Phase 1 |
| M6 | §8.3, §9, §10 | Phase 1 |
| M7 | §5.4 | Phase 2 |
| M8 | §6.4, §10 | Phase 2 |
| M9 | §5.5, §11 (contract) | Phase 3 |
| M10 | REVIEW.md hardening + quality gates | Post-M9 |

All plan items trace to a `SPEC.md` requirement ID; `SPEC.md` §17 in turn traces
every requirement back to `IDEA.md`. No item in this plan introduces scope beyond
those two documents.

---

## 9. Development & test environment

Every milestone uses the same four-step loop — **build → run → drive → assert** —
against a matching nginx source tree. This section defines that harness.

### 9.1 Strategy: Docker authoritative, macOS optional inner loop

| Path | Role | Why |
|------|------|-----|
| **Docker (Linux)** | **Authoritative** — dev, CI, version matrix, all `AC-*` gates. | The product targets Linux (the Layer-4 eBPF add-on is Linux-only); Layer-2 pointer wrapping (M7) is ABI/version-fragile and needs a **pinned, reproducible** nginx build + matrix; **Valgrind** is Linux-only in practice; `dladdr()` name resolution (M7.3) is ELF-specific; CI and local loop are then identical. |
| **macOS (Homebrew source build)** | **Optional fast inner loop** for pure-C logic (conf, JSON, serialization). | Fastest edit-compile-run on the workstation; ASan works. **Not authoritative:** no Valgrind on Apple Silicon, ABI ≠ prod, Homebrew nginx version drifts, Mach-O ≠ ELF. |

**Rule:** a milestone is only "done" when it passes in **Docker** (§7). macOS is
for convenience smoke checks, never the sign-off.

### 9.2 Layout additions

```
ngx-trace/
├── docker/
│   ├── Dockerfile.dev            # pinned nginx source + toolchain + module build
│   ├── docker-compose.test.yml   # nginx + upstream(s) + client/test-runner
│   └── nginx.test.conf           # loads the .so; trace_zone/control/upstreams
├── scripts/
│   └── mac-build.sh              # optional: Homebrew-source build (NOT authoritative)
├── test-backends/                # recording echo, flaky (502→200), gRPC, streaming
├── justfile                      # build / test / test-asan / test-valgrind / matrix / bench
└── t/                            # Perl .t suite (one file per AC group)
```

### 9.3 `docker/Dockerfile.dev` (skeleton)

Pinned nginx built with the module via `--add-dynamic-module`, debug symbols,
and the flags the module needs (`--with-compat`, HTTP/2 for gRPC, stream, debug).
`NGINX_VERSION` is a build-arg so the same file drives the whole version matrix.

```dockerfile
FROM debian:bookworm-slim
ARG NGINX_VERSION=1.27.0
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential ca-certificates curl \
      libpcre2-dev zlib1g-dev libssl-dev \
      valgrind gdb git perl libtest-nginx-perl \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
# nginx source MUST match the runtime nginx version (see Jiri-Mihal approach)
RUN curl -fL https://nginx.org/download/nginx-${NGINX_VERSION}.tar.gz | tar xz
COPY . /src/ngx-trace
WORKDIR /src/nginx-${NGINX_VERSION}
# ASan toggled by build-arg for the test-asan target
ARG SANITIZE=
RUN ./configure --with-compat --with-debug \
      --with-http_v2_module --with-http_ssl_module \
      --with-http_auth_request_module --with-stream \
      ${SANITIZE:+--with-cc-opt="-fsanitize=address -g" --with-ld-opt="-fsanitize=address"} \
      --add-dynamic-module=/src/ngx-trace \
    && make modules && make install
# module .so is now in objs/ngx_http_trace_module.so
```

- Follows the Jiri-Mihal recipe's core idea (matching nginx source + rebuild),
  but **pins** the version and toolchain for reproducibility — `skill:conf-build-config`.
- The `nginx` runtime and the build stage share `NGINX_VERSION`; `load_module`
  points at the built `.so` in `nginx.test.conf`.

### 9.4 `docker/docker-compose.test.yml` (skeleton)

```yaml
services:
  nginx:                 # module under test
    build: { context: .., dockerfile: docker/Dockerfile.dev }
    volumes: [ "./nginx.test.conf:/usr/local/nginx/conf/nginx.conf:ro" ]
    ports: [ "8080:8080" ]
    depends_on: [ upstream, flaky, grpc-backend ]
  upstream:              # records exact received bytes → AC-3
    build: { context: ../test-backends, dockerfile: echo.Dockerfile }
  flaky:                 # 502 then 200 → AC-4
    build: { context: ../test-backends, dockerfile: flaky.Dockerfile }
  grpc-backend:          # non-zero grpc-status w/ :status 200 → AC-5
    build: { context: ../test-backends, dockerfile: grpc.Dockerfile }
  client:                # test-runner / traffic driver (wrk, grpcurl, perl .t)
    build: { context: ../test-backends, dockerfile: client.Dockerfile }
    command: [ "sleep", "infinity" ]
    depends_on: [ nginx ]
```

- `upstream` is the recording backend that makes **byte-exact** assertions
  (`AC-3`) possible; `flaky`/`grpc-backend` are the retry/gRPC fixtures.
- `nginx.test.conf` sets `worker_processes 4` for the cross-worker test (`AC-12`),
  enables `trace_zone`/`trace_control`, and defines the upstreams.

### 9.5 `justfile` targets (the dev API)

```make
build         : # docker compose build nginx
up            : # docker compose up -d  (nginx + backends)
down          : # docker compose down -v
sh            : # exec into the client container (curl/grpcurl/wrk)
test    *T    : # run Test::Nginx .t files inside the container (default: all)
test-asan *T  : # rebuild with SANITIZE=1, run .t under ASan
test-valgrind : # run nginx under valgrind --leak-check=full over the suite
test-matrix *T: # loop NGINX_VERSION over the pinned set, run .t on each  (M7/M9)
bench         : # wrk: module-loaded-no-session vs unloaded  → AC-1
mac-build     : # scripts/mac-build.sh  (optional, non-authoritative)
```

- `test`, `test-asan`, `test-valgrind` are the three gates in the per-milestone
  Definition of Done (§7); `test-matrix` is required for M7 and the M9 release
  sign-off (ABI-sensitive work); `bench` guards `NFR-PERF-1`/`AC-1`.

### 9.6 Test framework

- **`t/*.t`** use `Test::Nginx` (`libtest-nginx-perl`) — the same framework
  nginx itself uses — one file per AC group, mapping directly to the §5 table and
  the per-milestone "How to test" lines.
- **Backends** (`test-backends/`): a recording echo server (captures raw bytes
  for `AC-3`), a flaky server (`AC-4`), a gRPC server returning a non-zero trailer
  status (`AC-5`), and a streaming/chunked endpoint (M8.3).
- **Sanitizer gate** (`nginx-c-module-debug` skill): `test-asan` for
  use-after-free/overflow, `test-valgrind` for leaks (mandatory on the slab/ring
  paths at M5 and the body-filter paths at M8), `gdb` on any coredump.

### 9.7 `scripts/mac-build.sh` (optional inner loop)

A cleaned-up, pinned variant of the Jiri-Mihal recipe for quick local checks:
`brew install nginx`, download the **matching** `nginx -v` source, take
`nginx -V` flags, **strip existing `--add-dynamic-module`**, append
`--add-dynamic-module=$(pwd)`, `make modules`, copy `objs/*.so` into the Homebrew
modules dir, `load_module` in the local `nginx.conf`. Marked non-authoritative:
use `just test` (Docker) before considering anything done.

### 9.8 CI — GitHub Actions

**GitHub is the host** (repo, PRs, issues, releases) **and** the CI runner.
GitHub-hosted `ubuntu-latest` runners *are* the Linux target platform, so CI
tests the real ELF ABI — more correct than a macOS workstation. Two loops:

- **Local inner loop** — `just test t/<one>.t` (seconds) for every code change.
- **CI outer loop** — the full suite, ASan, and the version matrix on PRs; the
  slow jobs (Valgrind, bench) nightly. CI uses the **same `Dockerfile.dev`** as
  local dev, so there is **no drift** (`skill:conf-build-config`).

**Why it is not slower** (the two speed levers):

1. **Docker layer cache** (`cache-from/to: type=gha`) keys the nginx compile on
   `NGINX_VERSION`, so module-only changes skip the ~1–2 min nginx rebuild
   (job drops to ~30–60 s).
2. **Matrix runs in parallel** — the `AC-13` version sweep (M7/M9) executes each
   `NGINX_VERSION` on a separate runner concurrently, so wall-time ≈ one leg —
   *faster* than the sequential local `just test-matrix`.

**PR gate** (`.github/workflows/ci.yml`, skeleton):

```yaml
name: ci
on: { pull_request: {}, push: { branches: [main] } }
jobs:
  test:
    runs-on: ubuntu-latest
    strategy:
      fail-fast: false
      matrix:
        nginx: ["1.27.0", "1.26.2", "1.24.0"]   # AC-13 version sweep, parallel
    steps:
      - uses: actions/checkout@v4
      - uses: docker/setup-buildx-action@v3
      - name: Build dev image (cached nginx layer)
        uses: docker/build-push-action@v6
        with:
          context: .
          file: docker/Dockerfile.dev
          build-args: NGINX_VERSION=${{ matrix.nginx }}
          load: true
          tags: ngx-trace-dev:${{ matrix.nginx }}
          cache-from: type=gha,scope=nginx-${{ matrix.nginx }}
          cache-to:   type=gha,scope=nginx-${{ matrix.nginx }},mode=max
      - run: just test        # Test::Nginx suite  → AC-2..18
      - run: just test-asan    # sanitizer gate (§7 DoD)
```

**Nightly** (`.github/workflows/nightly.yml`): `schedule: cron` running
`just test-valgrind` (leak gate, 10–30× slow — off the PR critical path) and
`just bench` (`AC-1`/`NFR-PERF-1` overhead regression).

**eBPF caveat (Layer 4 / Rust add-on only):** uprobe attachment needs
`CAP_BPF`/BTF and sometimes `--privileged`; GitHub-hosted runners usually have
BTF but attachment can be flaky. If it is, pin the **eBPF** job to a
**self-hosted runner** or test it manually — the C core (M0–M10) has **no such
requirement** and runs on stock `ubuntu-latest`.

---

## 10. Spec/implementation divergence register

> **Purpose:** `SPEC.md` was reconciled to v0.2 against the M0–M8 code on
> 2026-07-26. Wherever the two disagreed, the *document* was changed to describe
> the code — which is correct for documentation but **not** automatically correct
> as a decision. This register records every such change so each can be
> adjudicated deliberately rather than silently inherited.
>
> **Verdict column:**
> - **code-was-right** — the implementation is better; the spec was aspirational
>   or wrong. Nothing to do beyond the doc change already made.
> - **needs-decision** — a real product choice was made implicitly by whoever
>   wrote the code. Someone should confirm or reverse it.
> - **spec-was-right** — the spec expressed a genuine requirement the code does
>   not meet. **These are open defects, not settled decisions.**

### 10.1 Configuration divergences

| # | Item | Spec said (v0.1) | Code does | Verdict |
|---|------|------------------|-----------|---------|
| D1 | `trace_max_sessions` default | `8` | `4` | code-was-right — SPEC v0.2 reconciled to match (FR-CFG-9 says `4`). 4 is conservative with the table holding 32. |
| D2 | `trace_max_transactions` default | `50` | `200` | **code-was-right (M9)** — the M9 default now clamps to ring capacity (64). `trace_max_transactions` is initialized to `NGX_HTTP_TRACE_RING_SLOTS` (64) in `src/ngx_http_trace_module.c:220`, and the `max=` query arg on session creation is clamped by `ngx_min()` to the ring capacity. Verified by `t/emit.t` tests 1–3. SPEC FR-CFG-10 updated to `64` post-M10. |
| D3 | `trace_retention` default | `1h` | `24h` (`86400000` ms) | code-was-right — SPEC v0.2 reconciled to match (FR-CFG-14 says `24h`). 24 h at rest is a material privacy posture but is the deliberate product default. |
| D4 | `trace_log_level` default | `info` | `error` | code-was-right for the value (SPEC v0.2 reconciled: FR-CFG-16 says `error`). **Separate gap:** FR-LOG-4 still mandates lifecycle logging "at `info` and above", which is silent by default. Either the default should be `info` or the requirement should accept `error` as the baseline for lifecycle events. See REVIEW.md G3. |
| D5 | `trace_control` grammar | `on\|off` flag | no-args directive | code-was-right — matches nginx convention for handler-installing directives (`stub_status`). |
| D6 | `trace_upstream_capture`, `trace_grpc_proto`, `trace_body_capture`, `trace_body_max` contexts | http, location | http, **server**, location | code-was-right — server scope is what FR-CFG-17's inheritance chain implies. |
| D7 | Default redaction set | `authorization cookie set-cookie` | adds `proxy-authorization` | code-was-right — strictly safer, same class of secret. |
| D8 | `trace_fault_only` | absent from §4.1 (only referenced via FR-SEL-4) | implemented directive, `on\|off [code]` | code-was-right — now documented as FR-CFG-20. |
| D9 | `trace_hardened` | implied by NFR-SEC-7, no directive specified | implemented, main-only flag | code-was-right — now documented as FR-CFG-21. |
| D10 | Per-session `watch`/`redact` overrides | FR-CFG-19 requires them creatable via API | not implemented; directive-scoped only | **deferred** — moved to IDEA.md §17 future ideas. |

### 10.2 Schema divergences (§8.3)

These matter more than they look: §8.3 field names are declared *normative*, so
any external consumer written against v0.1 will not match what ships.

| # | Item | Spec said | Code emits | Verdict |
|---|------|-----------|------------|---------|
| D11 | Summary path field | `path` | `uri` | code-was-right — SPEC v0.2 reconciled; the normative schema (§8.3) uses `uri`, matching nginx vocabulary. |
| D12 | Status field | `final_status` | `status` | code-was-right — SPEC v0.2 reconciled; the normative schema (§8.3) uses `status`. |
| D13 | Transaction/summary identity | `id` + `session_id` | `seq` only (session implied by route) | code-was-right — the route already carries the session; a duplicated id invites disagreement. |
| D14 | Transaction detail fields | `worker_pid`, `connection_id`, `started_at`, `duration_us`, `client`, `request_line`, nested `summary` | none of these | **spec-was-right (resolved M9)** — `worker_pid` and `connection_id` are now emitted in the transaction JSON (`src/ngx_http_trace_json.c`). Verified by `t/emit.t` tests 4, 5, and 16. `client` and `request_line` remain outstanding for future work. |
| D15 | Step fields | `result`, `condition` text, `headers_in_delta`, `response_headers_delta`, `location`, `properties`, per-step `upstream` | `evaluated` bool; the rest absent | **deferred** — moved to IDEA.md §17 future ideas (per-step detail enrichment). |
| D16 | Session `filter.match`/`method` | specified | not implemented (only `path_prefix`) | **deferred** — moved to IDEA.md §17 future ideas (richer session matching). |
| D17 | Body shape | `content_type`, `captured_bytes`, `truncated` | adds `total_bytes`, `content_encoding` | code-was-right — `total_bytes` is what makes `truncated` interpretable; now FR-JSON-4. |

### 10.3 Behavioral divergences

| # | Item | Spec said | Code does | Verdict |
|---|------|-----------|-----------|---------|
| D18 | `CONTENT` phase observer | FR-PHASE-1 lists `CONTENT` | no phase handler; content reached via Layer-2 slot wrap | code-was-right — a `phases[CONTENT]` handler is dead code once `clcf->handler` is set, which is the normal case. |
| D19 | Request-body capture mechanism | FR-BODY-3: use async `ngx_http_read_client_request_body` | snapshots at LOG; never initiates a read | code-was-right — strictly better against FR-BODY-2 (cannot perturb timing) and still satisfies "never block". |
| D20 | Response body vantage point | "top output body filter", client-facing bytes | head of chain → **pre-gzip plaintext** | **deferred** — moved to IDEA.md §17 future ideas. Genuinely useful (you see what the app produced), but it is *not* "what the client received". If byte-exact-on-the-wire is wanted, the filter must move to the tail. Documented as FR-BODY-6. |
| D21 | `sendfile`/`in_file` responses | not addressed | `total_bytes` reported, `captured_bytes` = 0 | code-was-right — reading them means blocking I/O in a filter (G8). Documented as FR-BODY-7. |
| D22 | Session creation payload | "duration/TTL, max, filter, watch, redact" implying a JSON body | query arguments; TTL accepted | **code-was-right (resolved M9)** — the `ttl` query argument (seconds, converted to ms) is now accepted and applied on both `POST /sessions` and `GET /session`. Verified by `t/emit.t` tests 6–8. |
| D23 | `POST /import` semantics | "accept for offline viewing" | validates only; never re-injects into shm | code-was-right — re-injection would let an unauthenticated POST forge records into a live ring. Documented as FR-API-14. |
| D24 | Oversized transactions | not addressed | dropped whole; visible only in diagnostics log | **spec-was-right (open defect)** — `REVIEW.md` #6. Now more reachable since body previews compete for the same 8 KB slot. Documented as CON-CFG-2. |

### 10.4 Requirements with no implementation and no test

Tracked here so they are not mistaken for done. None of these were claimed by
M0–M8; they are listed because §15 acceptance criteria reference them.

| Requirement | Status | Blocking AC |
|---|---|---|
| FR-L3-1/2/3 — `ngx_trace_step()` emit API | built (M9.1) in `src/ngx_http_trace_ctx.c:549-596`; tests in `t/emit.t` | AC-14 |
| njs/Lua `trace.step()` binding (M9.2) | deferred — core C API is the stable contract; script binding is a thin wrapper around it | — |
| FR-GRPC-1/3/4/5 — HPACK decode, per-message framing, proto decode | deferred — moved to IDEA.md §17 future ideas (gRPC depth). Protocol detection and `grpc-status`/`grpc-message` work (FR-GRPC-2). | AC-5 partially |
| FR-EBPF-* — Layer 4 | not built | — |
| FR-UI-4 (Properties, header diffs), FR-UI-6 (persisted view options) | deferred — moved to IDEA.md §17 future ideas (per-step detail). UI panes exist; underlying step data absent (was D15). | AC-17/18 unaffected |
| AC-1 — overhead benchmark | built (M10.7) — `make bench` target added | AC-1 |
| `test-valgrind` | built (M10.7) — `make test-valgrind` target added | — |

### 10.5 M9 & M10 resolution summary

| Resolved | Scope | Issue from REVIEW.md | Milestone |
|----------|-------|---------------------|-----------|
| D2 | `trace_max_transactions` clamped to ring (64) | §10.1 | M9 |
| D14 | `worker_pid`/`connection_id` in JSON | §10.2 | M9 |
| D22 | TTL query arg on session creation | §10.3 | M9 |
| M10.1 | `dropped` counter race | REVIEW.md NEW #4 | M10 |
| M10.2 | `duration_us` overflow | REVIEW.md NEW #13 | M10 |
| M10.3 | Session JSON buffer (512→1024) | REVIEW.md NEW #14 | M10 |
| M10.4 | `fault_code` range validation | REVIEW.md #8 | M10 |
| M10.5 | `slot->fault_code` populated | REVIEW.md #9 | M10 |
| M10.6 | `response_logged` dedup flag | REVIEW.md #6 | M10 |
| M10.7 | `test-valgrind` + `bench` harness | REVIEW.md #10 | M10 |
| M10.8 | `decide()` mutex cost docs | REVIEW.md #3 | M10 |

### 10.6 Recommended remaining adjudication

1. **D24** — surface slot-overflow drops in the API, not just the log.
2. **D4/FR-LOG-4** — resolve the `trace_log_level error` default vs FR-LOG-4's
   mandate for lifecycle logging at `info`. Either raise the default or lower the
   requirement. See REVIEW.md G3.
3. **D20** — decide the response-body vantage point (head vs tail of filter
   chain) before an external consumer relies on the current pre-gzip behaviour.
4. **Future ideas** — D10 (per-session overrides), D15 (step detail enrichment),
   D16 (richer session matching), D20 (tail-of-chain body option), and gRPC depth
   (FR-GRPC-1/3/4/5) are captured in `IDEA.md` §17.
