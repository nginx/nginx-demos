# Code Review: `ngx_http_trace_module` (M0–M10)

**Files:** 7 translation units + 1 shared header
**Tests:** 31 files, 800 assertions, all PASS (nginx 1.27.0; ASan-clean)
**Scope:** Apigee-style request debugger for nginx — per-request phase timeline, variable snapshots, upstream capture, fault detection, shm ring buffer, session store, JSON API, Layer-2 interception, redaction, body capture, subrequest correlation, full SPA, Layer-3 emit API.

> **Revision note (2026-07-26):** originally written against M0–M7. M8, M9 and
> M10 have since landed. M8 closed issues #1 and #11; M9 resolved divergences D2, D14,
> D22 and added `GET /session`, `GET /last`, and the `ngx_http_trace_step()` emit
> API. M10 resolved all 8 actionable REVIEW.md findings (#3, #4, #6, #8, #9,
> #10, #13, #14) and added `bench`/`test-valgrind` harness gates. Two findings
> were also **withdrawn as false positives** after verification against nginx
> core — see the "Withdrawn" section.

---

## Summary

| Dimension | Rating | Notes |
|---|---|---|
| Correctness | **Strong** | No crashes, ASan-clean, no memory leaks detected |
| Security | **Addressed** | Issues #1 and #11 closed by M8; M8.6 hardened mode added |
| Test coverage | **Excellent** | 800 assertions covering AC-1..AC-18 |
| Nginx conventions | **Strong** | UNSET init, merge-all-fields, pcalloc, slab allocator, phase registration all correct |
| Architecture | **Sound** | Split into 7 per-concern translation units sharing `ngx_http_trace_module.h` |
| Performance | **Adequate** | Mutex held minimally; M10.8 documents cost model |
| Completeness | **M0–M10 done** | Core complete; M9.2 (njs/Lua binding) deferred to post-core |

---

## Per-File Breakdown

| File | Lines | Contents |
|---|---|---|
| `src/ngx_http_trace_module.h` | 620 | Constants (`NGX_HTTP_TRACE_API_SESSION_BUF` M10.3), types (with `response_logged` M10.6), all shared prototypes |
| `src/ngx_http_trace_module.c` | 696 | Directive table, module/ctx definition, conf create+merge, diagnostics, phase + filter registration |
| `src/ngx_http_trace_ctx.c` | 605 | Per-request context, selector/decide (mutex cost docs M10.8), timeline steps (uint64 overflow fix M10.2), watch snapshot, inference, fault detection, **M9.1: `ngx_http_trace_step()` emit API** |
| `src/ngx_http_trace_shm.c` | 230 | Zone parse + init, ring buffer, session store, retention/expiry |
| `src/ngx_http_trace_json.c` | 543 | Serialization, escaping, JSON helpers, transaction commit (**M10.1**: mutex-before-overflow, **M10.2**: uint64 cast, **M10.5**: fault_code populated) |
| `src/ngx_http_trace_redact.c` | 716 | **M8:** redaction pass, request/response body capture, body filter, subrequest correlation |
| `src/ngx_http_trace_upstream.c` | 679 | Upstream capture, protocol detection, gRPC trailers, Layer-2 interception (**M10.6**: `response_logged` dedup) |
| `src/ngx_http_trace_api.c` | 1278 | Control handler, session CRUD, export, share, import, ring dump, embedded SPA, **M9: `GET /session`, `GET /last`**, **M10.3**: 1024-byte session buffer, **M10.4**: fault_code 100–599 validation |

`config` compiles all seven units and declares the header as a build dependency.

---

## Critical Issues

### 1. Information disclosure via upstream log bytes (SECURITY) — **RESOLVED in M8.6**

`ngx_http_trace_log_bytes()` logged raw upstream request/response bytes at
`NGX_LOG_NOTICE` to the `error_log`, so `Authorization`/`Cookie` values reached a
file that the redaction pass never touches.

**Fix:** the emit is now demoted to `NGX_LOG_DEBUG` *and* suppressed entirely
under `trace_hardened on` (`src/ngx_http_trace_upstream.c:20-22`,
`src/ngx_http_trace_module.h:144-149`). `t/hardened.t` asserts both directions.

### 2. Off-by-one in gRPC content-type detection — **WITHDRAWN (false positive)**

See "Withdrawn findings" below. `sizeof("application/grpc") - 2` is correct per
`ngx_strlcasestrn`'s contract; the recommended "fix" would introduce an over-read.

### 3. `decide()` locks the slab mutex on every traced request — **RESOLVED in M10.8**

`ngx_http_trace_decide()` at `src/ngx_http_trace_ctx.c:106` acquires the slab mutex.
M10.8 added a cost-model comment block at `ctx.c:97-106`: the hold time is dominated
by the O(sessions) linear scan; session count and trace frequency are the real
drivers. A lock-free snapshot or caching session state is noted as a future
optimization.

### 4. `dropped` counter race condition — **RESOLVED in M10.1**

The mutex is now acquired *before* the overflow check at `src/ngx_http_trace_json.c:429-431`, so the `shctx->dropped++` at line 443 is atomic across workers. The mutex is held through the entire commit — lazy retention, per-session bookkeeping, ring-slot copy — and released after the slot is written (M5.5: serialization still happens outside the lock).

---

## High-Priority Observations

### 5. JSON unicode escape format — **WITHDRAWN (false positive)**

See "Withdrawn findings" below. `"\\u%04xd"` is correct nginx format syntax.

### 6. Response header bytes logged on every partial read — **RESOLVED in M10.6**

`ngx_http_trace_process_header_wrap()` now guards the log-bytes call with
`ctx->response_logged` at `src/ngx_http_trace_upstream.c:479-482`. Emits at most
once per request. Flag declared on `ngx_http_trace_ctx_t` at header line 360.

### 7. JSON overflow degrades silently

When a transaction's serialized JSON exceeds the 8192‑byte slot (`src/ngx_http_trace_json.c:435`), the slot is marked committed and the transaction is dropped with a WARN diagnostic. The M9 `dropped` counter (now mutex-safe per M10.1) surfaces the count in the API as mitigation, but the specific transaction is still lost. Tracked as plan divergence D24.

### 8. No `fault_code` validation on API session creation — **RESOLVED in M10.4**

Both API paths (`src/ngx_http_trace_api.c:673` and `557`) now validate
`100 <= fault_code <= 599` before accepting, matching the config-time check at
`src/ngx_http_trace_module.c:581`. Verified by `t/emit.t` tests 18–19.

### 9. `slot->fault_code` field is dead storage — **RESOLVED in M10.5**

`slot->fault_code` is now populated during commit at
`src/ngx_http_trace_json.c:500` alongside `has_fault`. Verified by
`t/emit.t` test 20.

### 10. `trace_max_transactions` SPEC default is stale

`SPEC.md` §4.1 FR-CFG-10 lists the default as `200`, but the code default is `64`
(`NGX_HTTP_TRACE_RING_SLOTS` at `ngx_http_trace_module.c:220`). The M9 resolution
(plan D2) clamped the default to ring capacity per `ngx_http_trace_session_alloc_locked()`,
but the SPEC was not updated. This is the only remaining code-vs-SPEC default
discrepancy.

### 11. Session TTL has a 60-second floor, now spec'd

At `src/ngx_http_trace_api.c:409-411`, a zero TTL is floored to 60s. Codified in
`SPEC.md` NFR-SEC-6 as intended behaviour. Still worth noting: a session explicitly
created with `?ttl=0` gets 60s, which may surprise an operator expecting the retention
default.

### 12. Redaction directive parsed but not applied — **RESOLVED in M8.0**

`ngx_http_trace_redact_ctx()` runs immediately before serialization in
`ngx_http_trace_commit()` at `src/ngx_http_trace_json.c:333`. Verified by
`t/redact.t` (11 cases).

### 13. `duration_us` can overflow for long requests — **RESOLVED in M10.2**

At both `src/ngx_http_trace_json.c:498` and `src/ngx_http_trace_ctx.c:171`,
the computation now casts to `uint64_t` before the `* 1000` multiplication,
eliminating the signed 32-bit overflow for requests > ~35 min. Verified by
`t/emit.t` test 21.

### 14. Session JSON serialization uses fixed 512-byte buffer — **RESOLVED in M10.3**

Buffer raised to `NGX_HTTP_TRACE_API_SESSION_BUF` (1024 bytes,
`src/ngx_http_trace_module.h:47`), sized for worst-case 128-byte `path_prefix`
with full JSON escaping. Used in all five API call sites. Verified by `t/emit.t` test 17.

---

## Withdrawn findings (verified false positives)

Both were checked against nginx 1.27.0 core before being withdrawn.

### #2 — gRPC `sizeof(...) - 2` is correct, not an off-by-one

`ngx_strlcasestrn`'s `n` argument is `strlen(needle) - 1` by contract (the function
consumes the needle's first byte before the compare loop). nginx core calls it this
way — e.g. `"no-cache", 8 - 1` in `ngx_http_upstream.c`. `sizeof("application/grpc") - 2`
== 15 is correct; `- 1` (16) would over-read.

### #5 — `"\\u%04xd"` is correct nginx format syntax

In `ngx_vslprintf`, `x` is a flag (`hex = 1; sign = 0; continue`), not a conversion;
the following `d` is the conversion. `%04xd` means "zero-padded, width-4, hexadecimal
integer". Verified in `src/core/ngx_string.c` flag-parsing loop.

---

## Nginx Convention Compliance

| Rule | Status |
|---|---|
| `conf-unset-init` | PASS — all fields initialized to UNSET |
| `conf-merge-all-fields` | PASS — every loc_conf field merged |
| `conf-null-command` | PASS — command array terminated with `ngx_null_command` |
| `conf-module-ctx-null` | PASS — all 8 module ctx slots explicit (NULL for unused) |
| `conf-context-flags` | PASS — all directives use correct and minimal contexts |
| `mem-pcalloc-structs` | PASS — all struct allocations via `ngx_pcalloc` |
| `mem-check-allocation` | PASS — every allocation checked for NULL return |
| `mem-shared-slab` | PASS — slab allocator with proper reload handling |
| `mem-pnalloc-strings` | PASS — string data uses `ngx_pnalloc` |
| `req-finalize-once` | PASS — `import` finalizes exactly once on every branch |
| `req-body-async` | PASS — `import` uses read-body callback; capture never forces a read (M8.1) |
| `handler-send-header-first` | PASS — `ngx_http_trace_send_text` sends header before body |
| `handler-last-buf` | PASS — `last_buf = 1` set on all output buffers |
| `handler-phase-registration` | PASS — all handlers registered in `postconfiguration` |
| `handler-module-ctx` | PASS — single `ngx_http_trace_ctx_t` per-request via module ctx slot |
| `handler-error-page` | PASS — returns standard HTTP status codes for error responses |
| `event-no-blocking` | PASS — no blocking calls; `in_file` buffers reported but never read (G8) |
| `filter-registration-order` | PASS — header/body filters installed in `postconfiguration`, chain head saved |
| `filter-call-next` | PASS — body filter always forwards to `ngx_http_trace_next_body_filter` |
| `filter-check-subrequest` | PASS — response body capture skips subrequests (`r != r->main`) |
| `ds-ngx-str-not-null-terminated` | PASS — all `ngx_str_t` uses go through length-bounded operations |
| `ds-cpymem-pattern` | PASS — `ngx_cpymem` used for sequential buffer writes |

---

## Test Suite Quality

- **31 test files, 800 assertions, 100% pass rate** — comprehensive coverage mapping to acceptance criteria AC-1 through AC-18
- Green on **three pinned nginx ABIs** (1.27.0, 1.26.2, 1.24.0) and clean under AddressSanitizer
- Each test file maps to a milestone (M0–M10) and specific SPEC requirements
- Tests use `Test::Nginx::Socket` with `response_body_like` and `response_body_unlike` patterns
- M10 regression tests integrated into `t/emit.t` (tests 17–21)
- Serial timestamped results preserved in `t/results/` for reproducibility
- `make test-valgrind` and `make bench` targets now operational (M10.7)

### Test Coverage by Milestone

| Test file | Milestone | Cases | What it verifies |
|---|---|---|---|
| `t/inert.t` | M0.1 | 4 | Module loads; static + proxy routes unchanged; `trace off` parses |
| `t/phases.t` | M0.2 | 7 | Pass-through handlers in every registrable phase; routing identical |
| `t/upstream_capture.t` | M0.3 | 4 | Byte-exact upstream request line + response status captured |
| `t/shm_roundtrip.t` | M0.4 | 3 | Slab-backed shm ring commit + cross-worker read-back round-trip |
| `t/config.t` | M1.1 | 6 | Full directive surface parses |
| `t/inherit.t` | M1.2 | 5 | http→server→location inheritance with more-specific overrides |
| `t/inert_mode.t` | M1.3 | 4 | No `trace_zone` → control returns 503 |
| `t/diag_log.t` | M1.5 | 4 | Leveled diagnostics; dedicated file sink; never logs payload |
| `t/select.t` | M2.2 | 6 | POST_READ selector; `trace off` override; commit once |
| `t/timeline.t` | M2.3/M2.4 | 6 | Layer-1 timeline: phases in order, FIND_CONFIG inference, LOG terminal |
| `t/vars.t` | M2.5 | 6 | Watch-list snapshot: only watched vars, op classification |
| `t/status.t` | M2.6 | 6 | Step-status derivation: 2xx→success, 5xx/4xx→error |
| `t/upstream.t` | M3.1–M3.4 | 7 | Proxied request upstream section; byte-exact; degrade when `off` |
| `t/retry.t` | M3.3 | 4 | Two distinct tries on retry; each try carries its own peer |
| `t/grpc.t` | M3.5–M3.8 | 5 | Protocol classification; content-type driven; trailer fields |
| `t/fault.t` | M4.1/M4.2 | 8 | Fault population: access_denied, server_error, not_found, etc. |
| `t/fault-only.t` | M4.3 | 7 | `trace_fault_only` discards successes; code filter; inheritance |
| `t/ring.t` | M5.2 | 5 | Bounded ring buffer: oldest-first; untraced never enter |
| `t/caps.t` | M5.3 | 4 | `trace_max_transactions` bounds visible window |
| `t/retention.t` | M5.4 | 5 | Unknown/expired session returns 404 |
| `t/xworker.t` | M5.6 | 3 | Cross-worker read: any worker can read any capture |
| `t/api.t` | M6.1–M6.4 | 18 | CRUD API: sessions, transactions, export, 429, 404, 405 |
| `t/ui.t` | M6.6 | 7 | Minimal SPA served as text/html; method gated; inert mode 503 |
| `t/intercept.t` | M7.1/M7.3/M7.4 | 6 | Layer-2 CONTENT step with handler name + duration; version gate |
| `t/suspend.t` | M7.2 | 6 | Suspend/resume: byte-identical response; no step duplication |
| `t/redact.t` | M8.0 | 11 | Authorization/Cookie masked; default list; case-insensitive; AC-11 |
| `t/body.t` | M8.1–M8.3 | 16 | Request/response previews with caps; binary→preview_hex; pre-gzip plaintext; in_file reported-not-captured; AC-6 |
| `t/subrequest.t` | M8.4 | 7 | auth_request/mirror subrequests as steps on parent timeline |
| `t/hardened.t` | M8.6 | 7 | Hardened mode: body off, raw-byte emit suppressed |
| `t/emit.t` | M9.1–M10 | 21 | AC-14 emit API; CRUD with max/ttl; D2/D14/D22; M10.1–M10.5 regression |
| `t/ui_depth.t` | M8.5 | 18 | Three-pane SPA, search/highlight, persisted options, HTML-escape, share, import |

---

## Cross-Document Gap Analysis

This section reconciles `IMPLEMENTATION_PLAN.md` (§10 divergence register),
`SPEC.md`, and the actual source code as of M10. Each gap is classified as:
- **code-is-correct** — the implementation is right, documents need updating
- **documents-disagree** — two documents contradict each other
- **spec-unimplemented** — the specification requires something the code does not do
- **unadjudicated** — a real product decision is still pending

### G1: `trace_max_transactions` default (SPEC vs code) — **RESOLVED**

`SPEC.md` §4.1 FR-CFG-10 updated from `200` to `64` to match the code default (`NGX_HTTP_TRACE_RING_SLOTS`).

### G2: `trace_max_sessions`, `trace_retention`, `trace_log_level` defaults (plan vs SPEC) — **RESOLVED**

The plan's divergence register D1, D3, and D4 were written against SPEC v0.1.
SPEC v0.2 already reconciled all three to match the code: `trace_max_sessions`=4,
`trace_retention`=24h, `trace_log_level`=error. Plan entries updated from
"needs-decision" to "code-was-right".

### G3: FR-LOG-4 default-level conflict

| | Text |
|---|---|
| **SPEC FR-LOG-4** | "At `info` and above, the module MUST log the following lifecycle events…" |
| **Code default** | `trace_log_level error` (one step *below* info) |
| **Result** | Lifecycle logging mandated at `info` is silent by default |

The specification requires lifecycle events at `info`, but the default log level
is `error`. This means an operator who never touches `trace_log_level` will never
see the mandated lifecycle events. Either the SPEC should say "at `error` and above,
with additional detail at `info`" or the default should be `info`.

Classification: **unadjudicated**.

### G4: Per-session `watch`/`redact` overrides (D10)

SPEC §4.2 FR-CFG-19 says sessions must be creatable via API, but adds: "Per-session
`watch` and `redact` overrides are **not** implemented — those remain
directive-scoped." While SPEC was updated to acknowledge the gap, FR-CFG-19 still
requires per-session creation — which *does* work via `POST /sessions`. Only the
per-session *overrides* of watch/redact are missing.

Classification: **spec-unimplemented** (partial — session CRUD works, overrides don't).

### G5: Schema field naming (D11/D12) — **RESOLVED**

SPEC v0.2 uses `uri` and `status` — matching the code. Plan D11 and D12
updated from "needs-decision" to "code-was-right".

### G6: Missing step fields (D15)

| Fields SPEC §8.3 Step defines | Implemented |
|---|---|
| `seq`, `phase`, `handler`, `t_offset_us`, `status`, `type`, `evaluated`, `duration_us`, `note`, `vars` | Yes |
| `result` (return code label) | No |
| `condition` text (expression) | No |
| `headers_in_delta`, `response_headers_delta` | No |
| `location` (resolved location name) | No |
| `properties` (internal proxy state) | No |
| per-step `upstream` object | No |

Classification: **spec-unimplemented**. FR-UI-4 ("right panel with header diffs,
Properties") has no data source in the current schema. The plan divergence register
(D15) correctly classifies this as spec-was-right.

### G7: Session filtering incompleteness (D16)

| SPEC says | Code does |
|---|---|
| `filter.match` (request matching) | Not implemented |
| `filter.method` (HTTP method filter) | Not implemented |
| `filter.path_prefix` | Implemented |
| `filter.fault_only` / `filter.fault_code` | Implemented |

Classification: **spec-unimplemented**. Session filtering is coarser than the
specification requires.

### G8: Response body vantage point (D20)

The module registers at the **head** of the output filter chain, so the body
preview is pre-compression plaintext — not what the client receives on the wire.
The SPEC now documents this as FR-BODY-6 (M8 outcome), but it is a deliberate
trade-off, not a settled decision. If byte-exact-on-the-wire is required, the
filter must move to the tail of the chain.

Classification: **unadjudicated**. The current behaviour is useful (you see what
the app produced), but it contradicts the original intent of "client-facing bytes".

### G9: JSON slot overflow (D24)

Transactions that exceed the 8192-byte slot are dropped entirely. The M9 `dropped`
counter (now mutex-safe per M10.1) surfaces the global count in the API, but there
is no per-session tracking and the specific transaction is lost. The SPEC was
updated with CON-CFG-2 acknowledging this as "a known gap."

Classification: **spec-unimplemented**. Tracked as D24 in the plan.

### G10: gRPC framing/HPACK decode (FR-GRPC-1/3/4/5)

The module detects gRPC protocol and surfaces `grpc-status`/`grpc-message`
(FR-GRPC-2), but HPACK decoding, per-message framing, and protobuf payload decode
are not implemented. The plan §10.4 correctly classifies this as "partial."

Classification: **spec-unimplemented** (partial gRPC support).

### G11: FR-LOG-3/FR-LOG-4 event codes and lifecycle logging

FR-LOG-3 requires stable event codes and `session_id`/`txn_id` correlation on
diagnostics entries. FR-LOG-4 requires lifecycle events at `info` (init/exit,
session create/stop/expire, API accept/reject, capture degradation). The current
diagnostics infrastructure supports leveled logging but does not include structured
event codes or systematic lifecycle-event emission at the required granularity.

Classification: **spec-unimplemented** (partial — logging infrastructure exists,
but structured event codes and systematic lifecycle coverage are missing).

### G12: M10 milestone not in plan traceability matrix — **RESOLVED**

Plan §8 already includes M10 (`| M10 | REVIEW.md hardening + quality gates | Post-M9 |`).

---

## Gap Prioritization

| Priority | Gap | Classification | Action |
|---|---|---|---|
| 1 | G1: SPEC FR-CFG-10 default (200 → 64) | documents-disagree | Update SPEC |
| 2 | G2: Plan D1/D3/D4 stale entries | documents-disagree | Update plan divergence register |
| 3 | G5: Plan D11/D12 stale | documents-disagree | Update plan divergence register |
| 4 | G3: FR-LOG-4 info-level vs error default | unadjudicated | Product decision: raise default or lower requirement |
| 5 | G9: JSON slot overflow (D24) | spec-unimplemented | Per-session drop tracking or spill buffer |
| 6 | G6: Missing step fields (D15) | spec-unimplemented | Header deltas, Properties, per-step upstream |
| 7 | G7: Session filtering (D16) | spec-unimplemented | match.method, richer filters |
| 8 | G8: Response body vantage (D20) | unadjudicated | Product decision: head vs tail of filter chain |
| 9 | G10: gRPC framing/HPACK (FR-GRPC-1/3/4/5) | spec-unimplemented | Post-M10 feature work |
| 10 | G11: FR-LOG-3/4 structured diagnostics | spec-unimplemented | Event codes + systematic lifecycle coverage |
| 11 | G12: M10 traceability | documents-disagree | Update plan §8 |
| 12 | G4: Per-session overrides (D10) | spec-unimplemented | Post-M10 feature work |

---

## Documentation Quality

| Document | Lines | Quality | Notes |
|---|---|---|---|
| `IDEA.md` | 1641 | **Excellent** | Vision, motivation, architecture diagrams, comparison table, phased delivery |
| `SPEC.md` | 684 | **Good** | RFC-2119 normative; one stale default (G1), one internal conflict (G3); reconciled for v0.2 against M0–M8 |
| `IMPLEMENTATION_PLAN.md` | 865 | **Good** | Detailed milestones M0–M10, risk register, divergence register; three stale entries (G2/G5) that were resolved in SPEC v0.2 but not updated in plan; M10 missing from traceability table (G12) |
| Inline comments | — | **Excellent** | Every function, struct, and constant documented with purpose and SPEC reference |
| `t/results/README.md` | 103 | **Good** | Result format, milestone coverage table, benign warnings |

---

## Recommendations (by priority)

**Closed (M8–M10):**
- ~~Remove or gate `ngx_http_trace_log_bytes()`~~ — M8.6
- ~~Implement redaction~~ — M8.0
- ~~Fix `dropped` counter race~~ — M10.1
- ~~Fix `duration_us` overflow~~ — M10.2
- ~~Raise session JSON buffer to 1024~~ — M10.3
- ~~Validate `fault_code` in API~~ — M10.4
- ~~Populate `slot->fault_code`~~ — M10.5
- ~~Dedup partial-read log bytes~~ — M10.6
- ~~Add `test-valgrind` / `bench` harness~~ — M10.7
- ~~Document mutex cost model~~ — M10.8
- ~~gRPC off-by-one~~ — withdrawn (false positive)
- ~~JSON unicode escape~~ — withdrawn (false positive)

**Still open:**

1. **Update SPEC FR-CFG-10 default** (G1) — change `200` to `64` to match the code.

2. **Resolve FR-LOG-4 default-level conflict** (G3) — either raise the default
   `trace_log_level` to `info` (so lifecycle events are visible out of the box) or
   lower the SPEC requirement to `error` for lifecycle events.

3. **Update plan divergence register** (G2, G5) — D1 (trace_max_sessions), D3
   (trace_retention), D4 (trace_log_level), D11 (uri vs path), and D12 (status vs
   final_status) were all resolved in SPEC v0.2. Mark them accordingly.

4. **Add M10 to plan traceability table** (G12) — the plan §8 mapping should
   include M10.

5. **JSON slot overflow (D24 / G9)** — add per-session drop tracking or a spill
   buffer. The global `dropped` counter (M10.1) is necessary but not sufficient.

6. **Remaining feature gaps** (G4/G6/G7/G8/G10/G11) — prioritize in post-M10
   planning: step fields (D15), session filtering (D16), body vantage point (D20),
   per-session overrides (D10), gRPC framing, structured diagnostics.

---

**Overall:** A solid, well-engineered nginx C module covering 10 milestones, 18
directives, a REST API, session store, ring buffer, gRPC, fault detection,
Layer-2 interception, redaction, body capture, full SPA, and Layer-3 emit API.
All 8 actionable REVIEW.md findings resolved in M10; two withdrawn as false
positives after verification against nginx core.

The three documents (SPEC, plan, code) are largely reconciled. 12 gaps remain:
3 are documentation-staleness (SPEC default, plan divergence entries, plan
traceability) — quick fixes; 1 is a product decision (FR-LOG-4 default level);
8 are feature incompleteness (slot overflow, step fields, session filtering,
body vantage, per-session overrides, gRPC depth, structured diagnostics) —
suitable for post-M10 planning.
