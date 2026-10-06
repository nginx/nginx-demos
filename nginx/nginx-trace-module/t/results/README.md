# Recorded test runs

Reusable, timestamped artifacts of the `ngx-trace` Test::Nginx suite, produced by:

```sh
make test-record                 # records for the default NGINX_VERSION (1.27.0)
make test-record NGINX_VERSION=1.26.2
```

Each run writes two files:

- `run-<nginx>-<UTC-timestamp>.txt` — the immutable record of that run.
- `latest.txt` — a copy of the most recent run (overwritten each time).

Every artifact captures, for reproducible milestone verification:

1. `nginx -V` — proves the module is compiled in (`--add-dynamic-module`).
2. `nginx -t` against `loadcheck.conf` — the M0.1 module-load check.
3. `prove -v t/` — full per-assertion TAP output.

## Interpreting the output

- A passing run ends with `All tests successful.` and `Result: PASS`.
- The `timeout when waiting for the process ... WARNING: killing the child
  process ... with force` lines are **benign** Test::Nginx teardown messages
  (the harness force-stops the worker at the end of each block); they do not
  indicate a failed assertion. Only `not ok` lines are real failures.

## Milestone coverage in the current suite

Current totals: **30 files, 721 assertions, Result: PASS** (nginx 1.27.0), covering M0–M8.

Verified across the full gate set:

| Gate | Result |
|------|--------|
| nginx 1.27.0 | 30 files, 721 tests, `Result: PASS` |
| nginx 1.26.2 | 30 files, 721 tests, `Result: PASS` |
| nginx 1.24.0 | 30 files, 721 tests, `Result: PASS` |
| ASan (`SANITIZE=1`, 1.27.0) | 721 tests PASS, zero memory errors |

The ASan run needs `ASAN_OPTIONS=detect_odr_violation=0:detect_leaks=0`: nginx emits
`ngx_module_names` in both the binary and the `.so` (a benign ODR duplicate for
dynamic modules), and nginx core never frees its config pool at exit. Neither is a
module defect. `make test-asan` now sets this itself, so the gate runs the suite
instead of aborting during startup; memory-error detection stays fully enabled.

| Test file             | Milestone | Cases | What it verifies                                            |
|-----------------------|-----------|-------|-------------------------------------------------------------|
| `t/inert.t`           | M0.1      | 4     | Module loads; static + proxy routes unchanged (inert); `trace off;` parses in location context and stays inert. |
| `t/phases.t`          | M0.2      | 7     | Pass-through handlers in every registrable phase; routing identical (rewrite, try_files, auth_request gate/allow, error_page, log, server-level SERVER_REWRITE). |
| `t/upstream_capture.t`| M0.3      | 4     | Byte-exact upstream request line + response status captured; exact `Host` header captured (header-level, not just request line). |
| `t/shm_roundtrip.t`   | M0.4      | 3     | Slab-backed shm ring: a traced request commits a JSON transaction into `trace_zone`; the `trace_control` endpoint reads it back out (cross-worker round-trip); reflects per-request method/uri/status; valid JSON with a steps array. |
| `t/config.t`          | M1.1      | 6     | Full directive surface parses in its declared context with correct defaults; directives rejected in wrong contexts (`trace_zone` in `location` → config error); `trace_zone <name> <size>` two-arg spec syntax; control endpoint available once a zone exists. |
| `t/inherit.t`         | M1.2      | 5     | `trace`, `trace_watch`, `trace_upstream_capture`, `trace_body_*`, `trace_redact` inherit http→server→location with more-specific overrides; enum/numeric directives inherit from http and a location overrides. |
| `t/inert_mode.t`      | M1.3      | 4     | With no `trace_zone`, config loads and the control endpoint returns 503; with a zone, control is available (not 503); inert mode is a true no-op — a `trace on` request commits nothing. |
| `t/diag_log.t`        | M1.5      | 4     | Leveled self-diagnostics: `trace_log_level debug` emits the commit milestone; default level short-circuits it (NFR-LOG-1); the commit diagnostic line carries only status/bytes metadata, never payload (FR-LOG-6); `trace_log <file>` routes diagnostics to a dedicated file sink. |
| `t/select.t`          | M2.2      | 5     | POST_READ selector: a `trace on` location is selected and committed; unselected locations commit nothing; `trace off` overrides inherited `trace on`; selection never alters routing; the latest traced request wins (commit exactly once). |
| `t/timeline.t`        | M2.3/M2.4 | 5     | Layer-1 timeline records registrable phases in order; each step carries `t_offset_us` + status; steps numbered by increasing `seq`; FIND_CONFIG inferred across a rewrite; LOG is always the terminal step appended last. |
| `t/vars.t`            | M2.5      | 5     | Watch-list snapshot: watched variable value captured; only watched vars appear; op classification recorded; bare (no leading `$`) watch name accepted; multiple watched variables all snapshotted. |
| `t/status.t`          | M2.6      | 5     | Step-status derivation: 2xx → success terminal step; 5xx and 4xx → error; top-level status mirrors HTTP status; a 3xx redirect is NOT an error (terminal step stays success). |
| `t/upstream.t`        | M3.1–M3.4 | 6     | Proxied request produces an `upstream` section with one try; byte-exact sent request line captured; received response header block + status recorded; `trace_upstream_capture off` degrades the section away (FR-UP-7); a non-proxied traced request has no upstream section; the try carries the state-derived `bytes` count and `request/response_truncated` flags. |
| `t/retry.t`           | M3.3      | 4     | A retried request records two distinct tries (from `r->upstream_states`); the final try carries the 200 status; a single successful upstream produces exactly one try; each try records its own `peer` (the dead peer vs the live one). |
| `t/grpc.t`            | M3.5–M3.8 | 5     | A plain HTTP proxied request is classified protocol `"http"`; plain HTTP tries carry no gRPC trailer fields; ordinary traffic is never misclassified as gRPC; classification is content-type-byte driven, not HTTP-method driven (a POST stays `"http"`); classification stays `"http"` across a multi-try retry harvest. |
| `t/fault.t`           | M4.1/M4.2 | 8     | Fault population at LOG: `auth_request` 401/403 → `summary.fault` with `error_state:access_denied` + `step_seq` linking the ACCESS step; 5xx → `server_error`; a success has no fault; nginx-originated 404 → `not_found`; an upstream 5xx → `upstream_error`; a 4xx client error → `client_error`. |
| `t/fault-only.t`      | M4.3      | 7     | `trace_fault_only on` discards successes and commits only faults; the optional `fault_code` filters by exact HTTP status (match commits, mismatch discards); `off` restores normal capture; a lone success leaves the ring empty; server-scoped `trace_fault_only` is inherited by a location. |
| `t/ring.t`            | M5.2      | 5     | Bounded transaction ring buffer: multiple committed transactions are all read back oldest-first; ordering is stable; a single traced request yields exactly one ring entry; un-traced requests never enter the ring. |
| `t/caps.t`            | M5.3      | 4     | `trace_max_transactions` bounds the visible window to the newest N (oldest evicted); a cap of 1 keeps only the most-recent transaction; below the cap all committed transactions remain; the default cap accepts a small burst untruncated. |
| `t/retention.t`       | M5.4      | 5     | `?session=<id>` for an unknown/expired session returns 404 (AC-15); `session=0` and non-numeric ids are rejected; the unfiltered ring read works alongside the session filter; a configured short `trace_retention` leaves the ring read available. |
| `t/xworker.t`         | M5.6      | 3     | Cross-worker read (real master + 4 workers): a transaction captured by any worker is read back through the shared slab; a burst across workers is fully visible from one control read; a multi-worker empty ring reads cleanly. |
| `t/api.t`             | M6.1–M6.4 | 18    | Routed control-plane API on a prefix location: `POST /sessions` creates a `TraceSession` (201); `GET /sessions` lists; `GET /sessions/{id}` detail; unknown id → 404; `DELETE` stops (`stopped_reason:manual`); a path-filtered session binds matching traffic; `.../transactions` returns `TransactionSummary` shapes; `.../transactions/{txn}` returns the full transaction; `.../export` returns session + transactions; `429` at `trace_max_sessions`; unknown txn → 404; wrong method → 405; unknown route → 404; the legacy exact-match control location still dumps the ring. Edge cases: a `fault_only`+`fault_code` session reflects its filter in the `TraceSession` shape; an empty session store lists `{"sessions":[]}`; a per-session `max` is clamped and reflected; a faulting transaction surfaces `fault:true` in the summary tier; an export of a session with no matching traffic returns `"transactions":[]`. |
| `t/ui.t`              | M6.6      | 7     | Minimal SPA served at `<prefix>/ui` as `text/html`; references the sibling `/sessions` API; GET/HEAD-gated (POST → 405); `DELETE` on the collection → 405; inert mode (no zone) → 503 for both API and UI; HEAD is allowed with an empty body. |
| `t/intercept.t`       | M7.1/M7.3/M7.4 | 6 | Layer 2 gated behind `trace_intercept on`: a named `CONTENT` step is appended carrying the resolved handler name (`"proxy"`) and a `duration_us`; the resolver derives the name from the upstream module, not a hardcoded symbol table; with `trace_intercept off` (default) the timeline stays Layer-1 (no CONTENT step); the version gate admits the supported nginx and degrades silently to Layer 1 otherwise; Layer 2 never alters routing or the response body. |
| `t/suspend.t`         | M7.2      | 6     | AC-16 — return codes preserved exactly across suspend/resume: a proxied request that suspends on `NGX_AGAIN` and resumes yields a byte-identical response and status with intercept on vs off; the CONTENT step is appended exactly once (no duplicate on resume); `duration_us` spans the whole suspended call, not just the first slice; an upstream error path still returns its code verbatim. |
| `t/redact.t`          | M8.0      | 11    | AC-11 / G6 — redaction happens *before* the shm write. `Authorization` and `Cookie` are masked with **no** `trace_redact` configured (the NFR-SEC-3 default-deny set); the header *name* survives so the timeline stays readable; an explicit list redacts custom names, matches case-insensitively, and *replaces* rather than extends the default set; a watched variable whose name matches is masked too (not only headers); the fixed-width mask leaks no length information; a redacted response header (`Set-Cookie`) never reaches the ring; masking does not corrupt the surrounding JSON; the client response is untouched (G1). |
| `t/body.t`            | M8.1/M8.2 | 16    | AC-6 / FR-BODY-1–5 — no body sections at all unless `trace_body_capture` is set (NFR-SEC-4); the response preview carries `captured_bytes`/`total_bytes`/`truncated`; a body over `trace_body_max` is **truncated, not dropped** (FR-BODY-3), and a `trace_body_max` above the hard ceiling is clamped (G3); enabling capture never alters the response bytes (G1); `Content-Type` is recorded alongside the preview; a POST body is captured when the content handler reads it (FR-BODY-1) and *nothing* is captured when it never does (FR-BODY-2 — no forced read); `both` captures the two directions independently while `request` alone leaves the response side empty; a gzipped response records `content_encoding` at LOG while the preview holds the pre-compression plaintext (FR-BODY-4); capture stays scoped to its location; a preview coexists with the `upstream` + `fault` sections; a sendfile'd static response is size-accounted but not read back (G8); a binary in-memory body is emitted as `preview_hex` (FR-BODY-5). |
| `t/subrequest.t`      | M8.4      | 7     | Subrequest correlation: an `auth_request` subrequest appears as its own step under the parent, named by the subrequest's own URI; a denying subrequest is marked `error`; the subrequest step and the parent's `fault` land in **one** transaction, not two; correlation never alters routing or the response (G1); an untraced parent records no subrequest steps (G2 fast path); the subrequest's own response body does not pollute the parent's body preview. |
| `t/hardened.t`        | M8.3      | 7     | NFR-SEC-7 `trace_hardened on` — a global kill switch that *overrides* an explicit `trace_body_capture` (bodies suppressed regardless of location config), proven against the identical config without it; the raw upstream-bytes debug emit is suppressed; tracing itself still works (the timeline stays intact) and upstream header capture is retained as redacted metadata; `off` behaves as the default; the directive is rejected outside the main context. |
| `t/ui_depth.t`        | M8.5/M8.6 | 18    | FR-UI-2–7 + FR-API-8/9 — the SPA ships the real three-pane layout fed by the transactions API, polling while a session captures; search highlights matches and auto-expands the groups containing them (FR-UI-5); view options persist per user (FR-UI-6); the detail panel renders bodies, the upstream section and gRPC trailers (FR-UI-4); sub-millisecond steps carry the epsilon marker (FR-UI-3); offline import is client-side and share copies a deep link (FR-UI-7); captured bytes are HTML-escaped before reaching `innerHTML`. API: `GET .../share` returns a deep link plus its expiry horizon, 404s an unknown session, and is GET-only; `POST /import` accepts an export and counts its transactions, rejects a non-export payload and an empty body, is POST-only, and **refuses a body buffered to disk rather than reading it back** (G8); an `export` round-trips through `import`; the UI derives its API base from its own request path (FR-UI-1). |

Newest test added per milestone:

- **M0.1 / TEST 4** — explicit `trace off;` parses and leaves routing unchanged.
- **M0.2 / TEST 7** — SERVER_REWRITE phase (server-context rewrite) unaffected.
- **M0.3 / TEST 4** — captured request bytes contain the exact upstream `Host` header.
- **M0.4 / TEST 1–3** — shm ring-buffer commit + control-endpoint read-back round-trip.
- **M1.1 / TEST 6** — a main-conf directive (`trace_zone`) is rejected in the `location` context (context enforcement).
- **M1.2 / TEST 5** — enum/numeric directives inherit from http and a location overrides.
- **M1.3 / TEST 4** — inert mode is a true no-op: a `trace on` request commits nothing without a zone.
- **M1.5 / TEST 4** — `trace_log <file>` routes diagnostics to a dedicated file sink (not just error_log).
- **M2.2 / TEST 5** — the latest traced request wins: commit overwrites, exactly once.
- **M2.3 / TEST 5** — LOG is always the terminal step, appended last in the timeline.
- **M2.5 / TEST 5** — multiple watched variables are all snapshotted in order.
- **M2.6 / TEST 5** — a 3xx redirect is not an error (terminal step stays success).
- **M3.1 / TEST 2** — the captured try holds the byte-exact sent request line.
- **M3.3 / TEST 1** — a retried request records two distinct tries.
- **M3.5 / TEST 1** — a plain HTTP proxied request is classified protocol `"http"`.
- **M3.2/M3.3 / `upstream.t` TEST 6** — the try carries the state-derived `bytes` count and both truncation flags.
- **M3.3 / `retry.t` TEST 4** — each try records its own `peer` (dead `127.0.0.1:1` vs the live backend).
- **M3.5 / `grpc.t` TEST 4** — classification is content-type driven, not method driven (a POST stays `"http"`).
- **M3.5 / `grpc.t` TEST 5** — classification stays `"http"` across a multi-try retry harvest.
- **M4.1 / `fault.t` TEST 7** — an upstream 5xx classifies `error_state:upstream_error`.
- **M4.2 / `fault.t` TEST 8** — a 4xx client error classifies `error_state:client_error`, determined at LOG.
- **M4.3 / `fault-only.t` TEST 7** — server-scoped `trace_fault_only` is inherited by a location.
- **M5.2 / `ring.t`** — bounded ring buffer commits and reads back multiple transactions oldest-first.
- **M5.3 / `caps.t`** — `trace_max_transactions` bounds the visible window to the newest N.
- **M5.4 / `retention.t`** — an unknown/expired `?session=<id>` returns 404 (AC-15).
- **M5.6 / `xworker.t`** — a transaction captured by any of 4 workers is read back through the shared slab.
- **M6.2 / `api.t` TEST 1** — `POST /sessions` creates a `TraceSession` (201) with `state:capturing` and `stopped_reason:null`.
- **M6.3 / `api.t` TEST 6–7** — a path-filtered session binds matching traffic; the list tier emits `TransactionSummary` shapes and the detail tier the full transaction.
- **M6.4 / `api.t` TEST 8** — `GET /sessions/{id}/export` returns the session plus its transactions.
- **M6.2 / `api.t` TEST 9** — `429 max_sessions_reached` when `trace_max_sessions` is hit.
- **M6.6 / `ui.t` TEST 1** — the minimal SPA is served at `<prefix>/ui` as `text/html`.
- **M6.2 / `api.t` TEST 14** — a `POST /sessions?fault_only=1&fault_code=500` session reflects `filter.fault_only`/`fault_code` in its `TraceSession` shape.
- **M6.2 / `api.t` TEST 15** — an empty session store lists as `{"sessions":[]}`.
- **M6.2 / `api.t` TEST 16** — a per-session `max` is clamped and reflected in `max_transactions`.
- **M6.3 / `api.t` TEST 17** — a faulting transaction surfaces `fault:true` in the summary tier.
- **M6.4 / `api.t` TEST 18** — exporting a session with no matching traffic returns `"transactions":[]`.
- **M7.1 / `intercept.t` TEST 1** — with `trace_intercept on`, a named `CONTENT` step carries the resolved handler name and `duration_us` (AC-13).
- **M7.3 / `intercept.t` TEST 2** — the handler name is derived from the upstream module (`"proxy"`), falling back to `"c-handler"`.
- **M7.4 / `intercept.t` TEST 4** — the version gate admits the supported nginx; on an unsupported build it warns and degrades to Layer 1.
- **M7.2 / `suspend.t` TEST 1** — a suspended/resumed proxied request is byte-identical with intercept on vs off (AC-16).
- **M7.2 / `suspend.t` TEST 3** — the CONTENT step is appended exactly once across `NGX_AGAIN` re-entry.
- **M8.0 / `redact.t` TEST 1** — `Authorization` is masked with *no* `trace_redact` configured: the default-deny set closes the G6 hole where a configured `trace_redact` previously did nothing (AC-11).
- **M8.0 / `redact.t` TEST 8** — the mask is fixed-width, so it does not leak the secret's length.
- **M8.1 / `body.t` TEST 10** — a location that never reads the request body captures nothing: proof the module does not force a read (FR-BODY-2 / G8).
- **M8.2 / `body.t` TEST 4** — a body over `trace_body_max` is truncated with `truncated:true` and an honest `total_bytes`, never dropped (FR-BODY-3).
- **M8.2 / `body.t` TEST 12** — a gzipped response records `content_encoding` while the preview holds pre-compression plaintext. This test *found a real bug*: `content_encoding` was being read in our header filter, which runs **before** `ngx_http_gzip_filter`'s, so the field was always empty; it is now resolved at LOG.
- **M8.2 / `body.t` TEST 15** — a sendfile'd static response reports its size but captures no bytes, because the buffer is `in_file` and reading it back would block (G8).
- **M8.4 / `subrequest.t` TEST 4** — a denying `auth_request` subrequest and the parent's fault appear in **one** transaction, confirming correlation rather than duplication.
- **M8.3 / `hardened.t` TEST 1** — `trace_hardened on` overrides an explicit `trace_body_capture`, proven against the identical config without it (NFR-SEC-7 is a true kill switch, not a default).
- **M8.5 / `ui_depth.t` TEST 3** — search highlights matches and auto-expands the groups containing them (FR-UI-5).
- **M8.6 / `ui_depth.t` TEST 8** — `GET .../share` returns a deep link plus its expiry horizon. This test *found a real bug*: the link was built with a double slash (`//ui`).
- **M8.6 / `ui_depth.t` TEST 15** — `POST /import` refuses a body nginx buffered to disk instead of reading it back, holding the G8 no-blocking-reads rule on the control plane too.
- **M8.6 / `ui_depth.t` TEST 16** — a session `export` round-trips through `import`, closing the FR-UI-7 offline loop end to end.
