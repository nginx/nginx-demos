# Development process: lessons & next-cycle agent workflow

## What we learned

### Documents work, but they drift

Four documents (IDEA, SPEC, PLAN, REVIEW) gave us clear handoffs, but keeping
them aligned against code required a dedicated reconciliation pass. The
divergence register (§10 of the plan) paid off — without it, several
code-was-right vs spec-was-right mismatches would have gone unnoticed.

### Small milestones beat big ones

M0 was a throwaway spike that validated the two make-or-break primitives
(upstream wrapping + shm round-trip) before anything else was built. M1–M9 each
delivered one verifiable capability. The alternative — building the whole thing
and testing at the end — would have buried bugs under 5000 lines of code.

### Tests before implementation catches design mistakes

Three M8 findings (Content-Encoding read timing, sendfile/`in_file` gap,
pre-compression vantage point) were design corrections the tests forced — not
coding bugs. Writing the test *before* the code would have surfaced them without
needing a rework pass.

### Context matters more than model size

The full review pass across 7 translation units + header + 4 documents took
significant context. Splitting the work into separate agent sessions (plan,
implement, test, review) with narrower scope per session would have been faster
and cheaper.

### Manual smoke catches what automation misses

The `make up` + httpbin setup surfaced the DNS/config-startup race, the
`trace off` on `__trace/` need, and the `max_sessions`/retention confusion —
none of which the Perl test suite would have caught because they're
configuration-ergonomic issues, not logic bugs.

---

## Next cycle: multi-agent pipeline

Each cycle runs one milestone step — at most ~10 minutes of agent time and
**≤ 40K tokens total** across all agents. Four separate agents, each with its own
prompt scope, model choice, and token budget.

### Architecture

```
                    ┌──────────┐
                    │  PLANNER  │  reads IDEA, SPEC, current plan
                    │  model:   │  writes next work item in plan
                    │  deepseek │  outputs: M-step number, SPEC refs,
                    └─────┬─────┘           done-when, files touched
                          │
                    ┌─────▼─────┐
                    │IMPLEMENTER│  reads plan work item + source files
                    │  model:   │  writes code + config
                    │  claude   │  skill: nginx-c-modules
                    └─────┬─────┘  outputs: changed files, line ranges
                          │
                    ┌─────▼─────┐
                    │  TESTER   │  reads plan work item + code diff
                    │  model:   │  writes t/*.t + runs make test
                    │  claude   │  outputs: test file, pass/fail
                    └─────┬─────┘
                          │
                    ┌─────▼─────┐
                    │ REVIEWER  │  reads code diff, test results
                    │  model:   │  updates REVIEW.md + plan outcome
                    │  deepseek │  reconciles divergence register
                    └─────┬─────┘  outputs: updated docs, gap report
                          │
                    ┌─────▼─────┐
                    │  MANUAL   │  reads MANUAL_TEST.md
                    │  SMOKE    │  runs make up + curl smoke tests
                    │  (human)  │  verifies expected output
                    └──────────┘
```

### Agent roles

| Agent | Reads | Writes | Model | Skills to load | Token budget | Time budget |
|---|---|---|---|---|---|---|
| Planner | IDEA, SPEC, plan | Plan work item | deepseek | `brainstorming`, `writing-plans` | 5K | 2 min |
| Implementer | Plan item, src/* | src/*, config | claude | `nginx-c-modules`, `test-driven-development` | 20K | 4 min |
| Tester | Plan item, code diff | t/*.t, test run | claude | `nginx-c-module-debug`, `verification-before-completion` | 8K | 3 min |
| Reviewer | Code diff, test results | REVIEW.md, plan outcome | deepseek | `receiving-code-review`, `systematic-debugging` | 7K | 3 min |
| Manual smoke | MANUAL_TEST.md | — | human | — | 0 | 1 min |

### Context minimization rules

| Agent | Budget | Spent on | Mitigation if over |
|---|---|---|---|
| Planner | 5K | Reading SPEC sections + plan, writing work-item row | Load only the SPEC section referenced by the work item, not the whole file |
| Implementer | 20K | Reading source + header, writing code | Read only the two files being changed; use `codebase_peek` for callers/callees |
| Tester | 8K | Reading diff, writing test, running `make test-one` | Test only the AC criteria, not every edge case in one cycle |
| Reviewer | 7K | Reading diff + test output, updating REVIEW.md + plan | Diff should be ≤ 100 lines; review only the changed code |

**Total per cycle: ≤ 40K tokens.**

Track tokens per cycle by logging the `usage` block from each API response
(prompt_tokens + completion_tokens). Append to a running tally in
`IMPLEMENTATION_PLAN.md` M-outcome after each cycle.

### Cycle token log (append to plan after each cycle)

```markdown
| Cycle | Agent | Prompt | Completion | Total |
|---|---|---|---|---|
| M10.1 | planner | 1,200 | 300 | 1,500 |
| M10.1 | implementer | 8,500 | 2,100 | 10,600 |
| M10.1 | tester | 3,200 | 800 | 4,000 |
| M10.1 | reviewer | 2,800 | 600 | 3,400 |
| **M10.1 total** | | | | **19,500** |
```

1. **Never load full source in planner/test/reviewer** — use `codebase_peek` to
   find where things are, then `read` only the relevant 50–100 line blocks.
2. **Index before starting** — run `index_codebase` at the start of each cycle
   so `codebase_search` and `codebase_peek` are fast.
3. **Use code-graph-rag** — `code-graph-rag_semantic_search` finds functions by
   behavior, `code-graph-rag_get_code_snippet` returns source by qualified name
   without loading files.
4. **Implementer is the only agent that reads full source files** — and only the
   ones it's touching.
5. **Pre-written prompts** — each agent gets a template prompt that includes:
   - The step's SPEC refs and done-when
   - File list (never glob — be explicit)
   - The 3–4 nginx skill rules relevant to the step
   - Expected output format
6. **Artifacts are compact** — the planner writes 5-line work items, the tester
   returns pass/fail + assertion count, the reviewer writes bullet findings.

### Per-cycle checklist

Before starting a cycle:
- [ ] `index_codebase` — refresh embeddings
- [ ] `index_health_check` — clean stale entries
- [ ] `code-graph-rag_index_repository` — update knowledge graph
- [ ] Load skills for the agent about to run (see table above)

Agent handoffs:
- [ ] Planner → verify work item references real SPEC IDs; save in plan
- [ ] Implementer → load `nginx-c-modules` skill; verify `config` still compiles: `make test-one T=t/inert.t`
- [ ] Tester → load `nginx-c-module-debug` + `verification-before-completion`; verify `make test-asan` passes on the new test
- [ ] Reviewer → load `receiving-code-review` + `systematic-debugging`; verify divergence register entries match code

After manual smoke:
- [ ] `make test-matrix` — verify across all three nginx ABIs
- [ ] Update test count in REVIEW.md header
- [ ] Commit with milestone prefix: `M<N>: <work-item>`

### Manual smoke template

At the end of each cycle, run this from `MANUAL_TEST.md`, plus any
milestone-specific checks:

```sh
make up
curl -s -X POST localhost:8080/__trace/sessions
# Drive traffic exercising the new feature
curl -s localhost:8080/__trace/sessions/1/transactions | python3 -m json.tool
# Verify: the new data appears in the expected schema shape
make down
```

### Model selection rationale

| Model | Used for | Why |
|---|---|---|
| **deepseek** | Planner, Reviewer | Fast inference, good at cross-document analysis, handles the divergence register's tabular format well |
| **claude + skills** | Implementer, Tester | nginx C module skill lives on Claude; needs the full 49-rule nginx convention guardrails during implementation |

The split reduces per-agent context: the implementer sees ~2000 lines of source
+ skill rules, not 5000 lines of source + 4 full-length documents + skill rules
+ test results.

### Skills by agent

**Planner** — `brainstorming` + `writing-plans`

- `brainstorming` ensures requirements are explored before committing to work items, preventing "build the wrong thing" cycles
- `writing-plans` structures the work item with SPEC refs, skill rules, done-when, and exit criteria in the plan's table format

**Implementer** — `nginx-c-modules` + `test-driven-development`

- `nginx-c-modules` gates every code change against 49 nginx conventions: pool allocation, request lifecycle, config UNSET/merge, handler registration, filter chain, upstream wrapping, event loop discipline
- `test-driven-development` requires the test to exist *before* the implementation code, so the implementer writes the test skeleton and then implements to make it pass

**Tester** — `nginx-c-module-debug` + `verification-before-completion`

- `nginx-c-module-debug` provides ASan setup, GDB invocation, coredump analysis should any test crash
- `verification-before-completion` requires `make test-asan` output to be shown before claiming success — no "it probably passes" claims

**Reviewer** — `receiving-code-review` + `systematic-debugging`

- `receiving-code-review` gives the reviewer a structured lens: verify technical correctness of the implementer's choices before suggesting changes
- `systematic-debugging` frames findings as hypothesis → evidence → root cause triples rather than vague observations, which maps directly to the divergence register's verdict columns

**Manual smoke** — operator reads `MANUAL_TEST.md` and runs `make up` + curl sequences. No AI agent — this is the human-in-the-loop checkpoint that catches configuration-ergonomic issues no test suite will find.

### Example: one cycle (adding a new directive)

```
PLANNER (target ≤ 5K tokens):
  Prompt:
  "Read SPEC.md §4 row for trace_foo and plan §10.
   Write work item: milestone, SPEC refs, done-when, files.
   Output: one table row. Do not read source files."
  → 1,500 tokens (1.2K prompt + 0.3K completion)

IMPLEMENTER (target ≤ 20K tokens):
  Prompt:
  "Read src/ngx_http_trace_module.c:188-310 and src/ngx_http_trace_module.h:165-180.
   Add trace_foo on|off directive with NGX_CONF_UNSET init, merge, and setter.
   Skill rules: conf-unset-init, conf-merge-all-fields, conf-context-flags.
   Verify: no new allocations un-freed, no blocking calls.
   Output: changed files and line ranges."
  → 10,600 tokens (8.5K prompt + 2.1K completion)

TESTER (target ≤ 8K tokens):
  Prompt:
  "Read the diff. Create t/foo.t: parses, inherits, default value.
   Run: make test-one T=t/foo.t.
   Skills: verification-before-completion (show make test-asan output).
   Output: pass/fail + assertion count."
  → 4,000 tokens (3.2K prompt + 0.8K completion)

REVIEWER (target ≤ 7K tokens):
  Prompt:
  "Read the diff, test output. Update REVIEW.md with findings.
   Check: UNSET init, merge-all-fields, NULL checks, no leaks.
   Reconcile plan §10 divergence register. Output: review diff, divergence status."
  → 3,400 tokens (2.8K prompt + 0.6K completion)

CYCLE TOTAL: 19,500 / 40,000 tokens
```
