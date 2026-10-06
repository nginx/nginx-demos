# AGENTS.md

Guidelines for AI agents working on `ngx_http_trace_module`.

## Project overview

An Apigee-style request debugger for nginx — captures per-request phase timelines,
variable snapshots, upstream byte-exact request/response, gRPC, fault detection,
session store, ring buffer, JSON API + SPA.  C dynamic module, built via
`--add-dynamic-module`, tests with Test::Nginx.

## Document hierarchy

| File | Role | When to update |
|---|---|---|
| `IDEA.md` | Vision, motivation, future ideas (§17) | New deferred features |
| `SPEC.md` | Normative requirements (FR-*/NFR-*/CON-*) | Add/remove/change requirements |
| `IMPLEMENTATION_PLAN.md` | Milestones (M0–M10), divergence register (§10) | New milestone or resolved divergence |
| `REVIEW.md` | Code review findings, gap analysis | After review pass |

**Rule:** when a requirement is removed from SPEC (deferred), add it to `IDEA.md §17`
and update the plan's divergence register.

## Development workflow

For every change, follow this order:

1. **Plan** — write the work item in `IMPLEMENTATION_PLAN.md` (milestone, SPEC refs, done-when)
2. **Spec** — ensure `SPEC.md` has the requirement (FR-*/NFR-*/CON-*)
3. **Implement** — change source in `src/` (C) or `config` (build)
4. **Test first** — add tests in `t/` **before** or alongside implementation
5. **Verify** — `make test && make test-asan`
6. **Record** — update test count in `REVIEW.md` and plan's M-outcome section
7. **Review** — update `REVIEW.md` with findings, cross-reference documents
8. **Reconcile** — fix stale entries in plan's divergence register (§10)

## Source layout

```
src/
├── ngx_http_trace_module.h     # all types, constants, prototypes
├── ngx_http_trace_module.c     # module def, directives, conf merge, postconf, diag
├── ngx_http_trace_ctx.c        # per-request ctx, timeline, watch, fault, emit API
├── ngx_http_trace_shm.c        # slab zone, ring buffer, session store, expiry
├── ngx_http_trace_json.c       # serialization, escaping, commit
├── ngx_http_trace_upstream.c   # upstream capture, gRPC, Layer-2 intercept
├── ngx_http_trace_redact.c     # redaction, body capture, body filter, subrequests
├── ngx_http_trace_api.c        # control handler, session CRUD, SPA, import
config                          # nginx addon build script
```

Group new code by lifecycle stage. Add prototypes to the shared header.

## Testing

- Framework: `Test::Nginx` (Perl, `t/*.t`)
- Run: `make test` (Docker, nginx 1.27.0)
- ASan gate: `make test-asan`
- Matrix: `make test-matrix` (1.27.0, 1.26.2, 1.24.0)
- Valgrind: `make test-valgrind`
- Single file: `make test-one T=t/file.t`
- Manual smoke: `make up` (nginx + httpbin), then `localhost:8080`

Every milestone (M0–M10) must pass all three gates: test, ASan, matrix.

## Nginx C conventions

Allocate structs with `ngx_pcalloc`. Check every return for NULL. Init config
fields to `NGX_CONF_UNSET*`, merge all fields in `merge_loc_conf`. Register
handlers in `postconfiguration`. Never block the event loop. Never access a
request after finalization. Hold slab mutex minimally.

See `.claude/skills/nginx-c-modules/` for the full rule set (49 rules across
8 categories).

## Milestone structure

Each milestone in the plan has:
- **Goal** — what the milestone delivers
- **Work items** — table with SPEC refs, skill rules, done-when
- **Exit criteria** — AC-* tests that gate completion
- **How to test** — specific `make test t/*.t` command
- **Outcome** — table of where code landed + test file, added after completion

## Divergence register

`IMPLEMENTATION_PLAN.md §10` tracks every SPEC-vs-code difference. Verdicts:
- `code-was-right` — implementation is correct, SPEC updated to match
- `needs-decision` — product choice made implicitly, needs deliberate confirmation → `unadjudicated`
- `spec-was-right` — the SPEC requires something the code doesn't do → open defect
- `deferred` — moved to `IDEA.md §17` future ideas

After implementing a milestone, walk the register and update any resolved entries.

## Review process

1. Read all source files, SPEC, and plan
2. Check every nginx convention (UNSET init, merge-all-fields, pcalloc, etc.)
3. Verify SPEC FR-* requirements against code
4. Check for: memory leaks, mutex races, overflow, truncation, null derefs
5. Cross-reference plan's divergence register against current code
6. Cross-reference SPEC defaults against code defaults
7. Report in `REVIEW.md` with per-file breakdown, critical/high/medium, convention compliance table
8. Add a cross-document gap analysis section reconciling SPEC, plan, and code

## Pull request checks

Before marking any work complete:
- [ ] `make test` passes (31 files, 800 tests)
- [ ] `make test-asan` passes (zero memory errors)
- [ ] `make test-matrix` passes (all three nginx versions)
- [ ] `REVIEW.md` updated with findings
- [ ] `IMPLEMENTATION_PLAN.md §10` divergence register reconciled
- [ ] Test count in `REVIEW.md` matches actual assertion count
