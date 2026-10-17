/*
 * ngx_http_trace_module - request context, timeline & fault detection.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_trace_module.h"

/*
 * Retrieve the per-request trace context, or NULL if the selector never ran /
 * the request is not traced. This is the single ctx-slot accessor used by both
 * the timeline observers and the upstream-capture spike (skill:handler-module-ctx).
 */
ngx_http_trace_ctx_t *
ngx_http_trace_get_ctx(ngx_http_request_t *r)
{
    return ngx_http_get_module_ctx(r, ngx_http_trace_module);
}

/*
 * M2.2 — POST_READ selector + no-trace fast path (G2, FR-SEL-1/2).
 *
 * Runs first in POST_READ. It creates the per-request ctx and captures the
 * t_offset baseline. It does NOT yet decide whether the request is traced:
 * `trace on` is most often set at *location* scope, and the location is not
 * resolved until FIND_CONFIG, which runs AFTER POST_READ. The decision is made
 * lazily by ngx_http_trace_decide() on the first observer that runs once the
 * location config is in effect (SERVER_REWRITE/REWRITE onward), then cached so
 * every later hook takes the G2 fast path with zero work.
 *
 * Always returns NGX_DECLINED: the selector MUST NOT alter control flow
 * (FR-PHASE-2).
 */
ngx_int_t
ngx_http_trace_post_read_handler(ngx_http_request_t *r)
{
    ngx_http_trace_ctx_t  *ctx;

    /* Subrequests share the main request's decision; skip creating a ctx here
     * (subrequest correlation is M8). Only trace the main request for M2. */
    if (r != r->main) {
        return NGX_DECLINED;
    }

    ctx = ngx_http_trace_get_ctx(r);
    if (ctx != NULL) {
        return NGX_DECLINED;            /* already created (re-entry) */
    }

    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_trace_ctx_t));
    if (ctx == NULL) {
        return NGX_DECLINED;            /* degrade: never break the request */
    }

    ctx->start_msec = ngx_current_msec;
    ctx->start_time = ngx_time();
    ctx->started = 1;

    ngx_http_set_ctx(r, ctx, ngx_http_trace_module);

    return NGX_DECLINED;
}

/*
 * Resolve and cache the trace decision for this request (M2.2). Evaluated
 * against the *currently effective* location config, so it must be called only
 * from a point at or after FIND_CONFIG. The first call latches ctx->no_trace;
 * subsequent calls are O(1). For M2 the rule is simply whether `trace on` is in
 * effect for the resolved location (active-session matching arrives in M5).
 * Returns 1 when the request is traced, 0 otherwise.
 */
ngx_int_t
ngx_http_trace_decide(ngx_http_request_t *r, ngx_http_trace_ctx_t *ctx)
{
    ngx_http_trace_loc_conf_t  *tlcf;

    if (ctx == NULL) {
        return 0;
    }

    if (ctx->decided) {
        return !ctx->no_trace;
    }

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_trace_module);

    ctx->decided = 1;
    ctx->no_trace = (tlcf == NULL || !tlcf->enable) ? 1 : 0;

    /*
     * M6.2 — session association. A traced request is bound to the newest
     * capturing session whose path_prefix filter matches r->uri. If no session
     * matches it stays session-agnostic (session_id 0, ring-only). Sessions
     * live in the slab, so matching is done under the zone mutex.
     *
     * Cost model (M10.8 / REVIEW.md #3): one mutex acquire per traced request
     * (at the first post-FIND_CONFIG phase observer), scanning up to
     * trace_max_sessions entries (default 4, max 32). The scan is O(sessions)
     * with a simple strncmp path_prefix test per entry — the mutex hold time
     * is dominated by the linear scan, not the lock acquisition itself. For
     * deployments with hundreds of active sessions, the scan length is the
     * real cost; session matching under mutex becomes a bottleneck only when
     * session count and trace frequency are both high.
     */
    if (!ctx->no_trace) {
        ngx_http_trace_main_conf_t  *mcf;
        ngx_http_trace_shctx_t      *shctx;

        mcf = ngx_http_get_module_main_conf(r, ngx_http_trace_module);
        if (mcf != NULL && mcf->shm_zone != NULL) {
            shctx = mcf->shm_zone->data;
            if (shctx != NULL && shctx->sessions != NULL) {
                if (shctx->nsessions > 0) {
                    ngx_shmtx_lock(&shctx->shpool->mutex);
                    ngx_http_trace_expire_locked(shctx, ngx_time());
                    ctx->session_id =
                        ngx_http_trace_session_match_locked(shctx, r->uri.data,
                                                            r->uri.len);
                    ngx_shmtx_unlock(&shctx->shpool->mutex);
                }
            }
        }
    }

    return !ctx->no_trace;
}

/*
 * Append an empty step to the append-only timeline (FR-CTX-2). Returns NULL if
 * the request is not traced, the per-request step cap is hit, or allocation
 * fails — the caller then simply records nothing (G7 degrade). The step's
 * status defaults to `success`; observers/inference refine it.
 */
ngx_http_trace_step_t *
ngx_http_trace_add_step(ngx_http_request_t *r, ngx_http_trace_ctx_t *ctx,
    const char *phase, const char *handler)
{
    ngx_http_trace_step_t  *step;

    if (ctx == NULL || ctx->no_trace) {
        return NULL;
    }

    if (ctx->steps == NULL) {
        ctx->steps = ngx_array_create(r->pool, 16,
                                      sizeof(ngx_http_trace_step_t));
        if (ctx->steps == NULL) {
            return NULL;
        }
    }

    if (ctx->steps->nelts >= NGX_HTTP_TRACE_MAX_STEPS) {
        return NULL;                    /* bounded: NFR-MEM-1 */
    }

    step = ngx_array_push(ctx->steps);
    if (step == NULL) {
        return NULL;
    }

    ngx_memzero(step, sizeof(ngx_http_trace_step_t));

    step->seq = ctx->seq++;
    step->phase.data = (u_char *) phase;
    step->phase.len = ngx_strlen(phase);
    step->handler.data = (u_char *) handler;
    step->handler.len = ngx_strlen(handler);
    step->status = NGX_HTTP_TRACE_ST_SUCCESS;
    step->type = NGX_HTTP_TRACE_STEP_PHASE;
    step->t_offset_us =
        (ngx_msec_int_t) (uint64_t) (ngx_current_msec - ctx->start_msec) * 1000;

    return step;
}

/*
 * M2.5 — watch-list variable snapshot (FR-VAR-1/2/3, NFR-PERF-2).
 *
 * Evaluates ONLY the variables named in the effective `trace_watch` list — never
 * the full variable set. For each name it records {value, op}:
 *   - `set_failed` when a watched variable exists but has no set_handler (it is
 *     read-only, so a `set` targeting it could not apply — Apigee `≠`, AC-7);
 *   - `set` when the variable currently holds a value (was assigned);
 *   - `read` otherwise (evaluated/read only).
 * Lazily-evaluated variables are force-evaluated by ngx_http_get_variable, with
 * no side effects beyond evaluation (FR-VAR-3). Failures degrade silently (G7).
 */
void
ngx_http_trace_snapshot_watch(ngx_http_request_t *r, ngx_http_trace_ctx_t *ctx,
    ngx_http_trace_step_t *step)
{
    ngx_http_trace_loc_conf_t   *tlcf;
    ngx_http_variable_t         *v;
    ngx_http_core_main_conf_t   *cmcf;
    ngx_http_variable_value_t   *vv;
    ngx_http_trace_var_t        *tv;
    ngx_str_t                   *names, name;
    ngx_uint_t                   i, key;

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_trace_module);
    if (tlcf == NULL || tlcf->watch == NULL || tlcf->watch == NGX_CONF_UNSET_PTR) {
        return;
    }

    step->vars = ngx_array_create(r->pool, tlcf->watch->nelts,
                                  sizeof(ngx_http_trace_var_t));
    if (step->vars == NULL) {
        return;
    }

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);

    names = tlcf->watch->elts;

    for (i = 0; i < tlcf->watch->nelts; i++) {

        name = names[i];

        /* Accept both "$var" and "var" spellings in trace_watch. */
        if (name.len > 0 && name.data[0] == '$') {
            name.data++;
            name.len--;
        }
        if (name.len == 0) {
            continue;
        }

        key = ngx_hash_strlow(name.data, name.data, name.len);

        vv = ngx_http_get_variable(r, &name, key);
        if (vv == NULL) {
            continue;
        }

        tv = ngx_array_push(step->vars);
        if (tv == NULL) {
            return;
        }
        ngx_memzero(tv, sizeof(ngx_http_trace_var_t));
        tv->name = name;

        if (vv->not_found || !vv->valid) {
            ngx_str_null(&tv->value);
        } else {
            tv->value.data = vv->data;
            tv->value.len = vv->len;
        }

        /*
         * Classify read vs set vs set_failed. Resolve the variable's definition
         * from the core variables hash (runtime-safe, unlike the config-time
         * ngx_http_get_variable_index). A watched variable with no set_handler
         * and not marked CHANGEABLE is read-only: a `set` on it could not apply
         * (set_failed, AC-7). Otherwise a present value means it was assigned
         * (set); an absent value means read-only-so-far (read).
         */
        tv->op = NGX_HTTP_TRACE_OP_READ;

        v = ngx_hash_find(&cmcf->variables_hash, key, name.data, name.len);
        if (v != NULL) {
            if (v->set_handler == NULL
                && (v->flags & NGX_HTTP_VAR_CHANGEABLE) == 0)
            {
                /* read-only variable: an assignment could not apply */
                tv->op = NGX_HTTP_TRACE_OP_SET_FAILED;
            } else if (!vv->not_found && vv->valid && vv->len > 0) {
                tv->op = NGX_HTTP_TRACE_OP_SET;
            }
        } else if (!vv->not_found && vv->valid && vv->len > 0) {
            tv->op = NGX_HTTP_TRACE_OP_SET;
        }
    }
}

/*
 * M2.4 — effect inference for phases without custom-handler support
 * (FIND_CONFIG / POST_REWRITE / POST_ACCESS), per CON-ARCH-1 / FR-PHASE-4.
 *
 * These phases run between the observer phases we DO see, so we infer their
 * effect from observable deltas: a change in $uri or the resolved location name
 * since the previous observer means FIND_CONFIG (re)selected a location. We
 * record that as an inferred step attributed to "core".
 */
void
ngx_http_trace_infer(ngx_http_request_t *r, ngx_http_trace_ctx_t *ctx,
    const char *phase)
{
    ngx_http_core_loc_conf_t  *clcf;
    ngx_http_trace_step_t     *step;
    ngx_str_t                  loc;
    ngx_uint_t                 uri_changed, loc_changed;

    if (ctx == NULL || ctx->no_trace) {
        return;
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    loc = (clcf != NULL) ? clcf->name : (ngx_str_t) ngx_null_string;

    uri_changed = 0;
    loc_changed = 0;

    if (ctx->last_uri.data == NULL
        || ctx->last_uri.len != r->uri.len
        || ngx_strncmp(ctx->last_uri.data, r->uri.data, r->uri.len) != 0)
    {
        uri_changed = (ctx->last_uri.data != NULL);   /* first pass isn't a change */
    }

    if (ctx->last_loc.data == NULL
        || ctx->last_loc.len != loc.len
        || (loc.len && ngx_strncmp(ctx->last_loc.data, loc.data, loc.len) != 0))
    {
        loc_changed = (ctx->last_loc.data != NULL);
    }

    if (uri_changed || loc_changed) {
        step = ngx_http_trace_add_step(r, ctx, "FIND_CONFIG", "core");
        if (step != NULL) {
            step->note.data = (u_char *) "location/uri selected";
            step->note.len = sizeof("location/uri selected") - 1;
        }
    }

    /* remember for next comparison (copy into the pool so it stays stable) */
    if (r->uri.len) {
        ctx->last_uri.data = ngx_pnalloc(r->pool, r->uri.len);
        if (ctx->last_uri.data != NULL) {
            ngx_memcpy(ctx->last_uri.data, r->uri.data, r->uri.len);
            ctx->last_uri.len = r->uri.len;
        }
    }
    if (loc.len) {
        ctx->last_loc = loc;    /* clcf->name is stable config memory */
    }
}

/*
 * M2.3 — the shared observer body. Each registrable phase registers a thin
 * wrapper that calls this with its phase label. It runs inference for the
 * intervening internal phases, appends a step {phase, t_offset_us}, and takes a
 * watch-list snapshot — but ONLY when the request is traced (FR-PHASE-3). It
 * never touches routing (returns NGX_DECLINED to the wrappers).
 */
ngx_int_t
ngx_http_trace_phase_observer(ngx_http_request_t *r, const char *phase,
    ngx_int_t can_decide)
{
    ngx_http_trace_ctx_t   *ctx;
    ngx_http_trace_step_t  *step;

    ctx = ngx_http_trace_get_ctx(r);
    if (ctx == NULL) {
        return NGX_DECLINED;            /* subrequest: no ctx */
    }

    /*
     * The trace decision depends on the resolved *location* config, which is
     * only in effect from FIND_CONFIG onward. SERVER_REWRITE runs BEFORE
     * FIND_CONFIG, so it must not latch the decision (it would see the server
     * default and wrongly cache no-trace). Such early phases only record when
     * the request was already decided as traced; otherwise they defer.
     */
    if (can_decide) {
        if (!ngx_http_trace_decide(r, ctx)) {
            return NGX_DECLINED;        /* G2 fast path once decided */
        }
    } else if (!ctx->decided || ctx->no_trace) {
        return NGX_DECLINED;
    }

    /* Infer any internal phase that ran since the last observer (M2.4). */
    ngx_http_trace_infer(r, ctx, phase);

    step = ngx_http_trace_add_step(r, ctx, phase, "");
    if (step != NULL) {
        ngx_http_trace_snapshot_watch(r, ctx, step);
    }

    return NGX_DECLINED;
}

/* Per-phase observer wrappers: one label each so the timeline names the phase
 * correctly. All decline (FR-PHASE-2) so routing is unchanged. SERVER_REWRITE
 * runs before FIND_CONFIG so it may not make the trace decision (can_decide=0).
 */

ngx_int_t
ngx_http_trace_obs_server_rewrite(ngx_http_request_t *r)
{
    return ngx_http_trace_phase_observer(r, "SERVER_REWRITE", 0);
}

ngx_int_t
ngx_http_trace_obs_rewrite(ngx_http_request_t *r)
{
    return ngx_http_trace_phase_observer(r, "REWRITE", 1);
}

ngx_int_t
ngx_http_trace_obs_preaccess(ngx_http_request_t *r)
{
    return ngx_http_trace_phase_observer(r, "PREACCESS", 1);
}

ngx_int_t
ngx_http_trace_obs_access(ngx_http_request_t *r)
{
    return ngx_http_trace_phase_observer(r, "ACCESS", 1);
}

/*
 * LOG-phase handler. LOG handlers are always invoked (there is no "declined"
 * short-circuit at LOG) and are expected to return NGX_OK. It must never fail
 * the request. For traced requests it appends the final LOG step, derives step
 * statuses from the finalizing HTTP status, and commits the captured
 * transaction into the shm zone exactly once (M2.7 / G5); otherwise a no-op.
 */
ngx_int_t
ngx_http_trace_log_handler(ngx_http_request_t *r)
{
    ngx_http_trace_ctx_t   *ctx;
    ngx_http_trace_step_t  *steps, *log_step;
    ngx_uint_t              i, status;

    ctx = ngx_http_trace_get_ctx(r);

    /*
     * Decide here too: a request that is finalized before any observer phase
     * runs (e.g. an early rewrite/return, or an internal redirect) may reach
     * LOG without decide() having been called. It is safe — the location is
     * long resolved by LOG.
     */
    if (!ngx_http_trace_decide(r, ctx)) {
        return NGX_OK;
    }

    /* Append the terminal LOG step and snapshot the final variable values. */
    log_step = ngx_http_trace_add_step(r, ctx, "LOG", "log");
    if (log_step != NULL) {
        ngx_http_trace_snapshot_watch(r, ctx, log_step);
    }

    /*
     * M2.6/M4.1 — derive step status from the finalizing status and, on a
     * 4xx/5xx, attribute the fault. The failing step is the last *observer*
     * phase that ran before finalization (e.g. ACCESS for an auth_request 401);
     * if the request was finalized before any observer phase (early return),
     * the terminal LOG step is the failing step. Both the marked step and the
     * fault link to the same seq so the timeline and summary.fault agree.
     */
    status = r->headers_out.status ? r->headers_out.status : r->err_status;

    if (status >= NGX_HTTP_BAD_REQUEST && ctx->steps != NULL
        && ctx->steps->nelts > 0)
    {
        ngx_http_trace_step_t  *fail_step;

        steps = ctx->steps->elts;

        /* Prefer the last non-LOG observer step; fall back to LOG itself. */
        fail_step = log_step;
        for (i = ctx->steps->nelts; i > 0; i--) {
            if (&steps[i - 1] != log_step) {
                fail_step = &steps[i - 1];
                break;
            }
        }

        if (fail_step != NULL) {
            fail_step->status = NGX_HTTP_TRACE_ST_ERROR;
        }
        if (log_step != NULL) {
            log_step->status = NGX_HTTP_TRACE_ST_ERROR;
        }

        ngx_http_trace_detect_fault(r, ctx, status, fail_step);
    }

    ngx_http_trace_commit(r);

    return NGX_OK;
}

/*
 * M4.1 — populate ctx->fault from the finalizing status and the failing step.
 * Runs at LOG (M4.2: the fault is fully determined before commit so it can gate
 * fault-only sessions). error_state is a coarse machine-readable label derived
 * from the status and whether the failure originated upstream; message is a
 * short human label carrying no payload bytes (FR-LOG-6). step_seq links to the
 * exact timeline step.
 */
void
ngx_http_trace_detect_fault(ngx_http_request_t *r, ngx_http_trace_ctx_t *ctx,
    ngx_uint_t status, ngx_http_trace_step_t *fail_step)
{
    ngx_http_trace_fault_t  *f = &ctx->fault;
    ngx_uint_t               upstream_failed = 0;

    f->have       = 1;
    f->status     = status;
    f->code       = status;
    f->step_seq   = (fail_step != NULL) ? (ngx_int_t) fail_step->seq : -1;

    if (fail_step != NULL) {
        f->phase   = fail_step->phase;
        f->handler = fail_step->handler;
    }

    /* Did any upstream attempt itself fail (5xx or dead peer)? (FR-UP) */
    if (ctx->tries != NULL && ctx->tries->nelts > 0) {
        ngx_http_trace_try_t  *tries = ctx->tries->elts;
        ngx_uint_t             i;

        for (i = 0; i < ctx->tries->nelts; i++) {
            if (tries[i].status == 0 || tries[i].status >= 500) {
                upstream_failed = 1;
                break;
            }
        }
    }

    if (status == NGX_HTTP_UNAUTHORIZED || status == NGX_HTTP_FORBIDDEN) {
        f->error_state = NGX_HTTP_TRACE_ES_ACCESS_DENIED;
        ngx_str_set(&f->message, "request denied");

    } else if (status == NGX_HTTP_NOT_FOUND) {
        f->error_state = NGX_HTTP_TRACE_ES_NOT_FOUND;
        ngx_str_set(&f->message, "not found");

    } else if (status < 500) {
        f->error_state = NGX_HTTP_TRACE_ES_CLIENT_ERROR;
        ngx_str_set(&f->message, "client error");

    } else if (upstream_failed) {
        f->error_state = NGX_HTTP_TRACE_ES_UPSTREAM_ERROR;
        ngx_str_set(&f->message, "upstream error");

    } else {
        f->error_state = NGX_HTTP_TRACE_ES_SERVER_ERROR;
        ngx_str_set(&f->message, "server error");
    }
}

/*
 * M9.1 — Layer-3 emit API (FR-L3-1/2, AC-14). ngx_http_trace_step() appends a
 * self-described step to the current request's trace timeline. When the request
 * is not traced the function returns immediately — no allocation, no work
 * (FR-L3-2 → O(1) no-op). Cooperating C modules and njs/Lua bindings call this
 * to self-report substeps at Apigee-style per-policy granularity.
 *
 * name   — step label (phase column), e.g. "auth-oidc"
 * result — "success" | "error" | "skipped" (NULL/non-matching → "success")
 * detail — short human note (NULL → none), appended as step->note
 *
 * Thread-safe: reads r->pool and ctx fields, appends only, never blocks.
 */
ngx_int_t
ngx_http_trace_step(ngx_http_request_t *r, const char *name,
    const char *result, const char *detail)
{
    ngx_http_trace_ctx_t   *ctx;
    ngx_http_trace_step_t  *step;
    ngx_uint_t              st;

    ctx = ngx_http_trace_get_ctx(r);
    if (ctx == NULL || ctx->no_trace) {
        return NGX_OK;                  /* O(1) no-op (FR-L3-2) */
    }

    /* Only add after the trace decision is latched — a cooperating module
     * calling us from an early phase (e.g. HTTP_REWRITE) must not trigger a
     * lazy decide on stale scope config. If we are not yet decided the caller
     * ran too early and should defer; we silently skip. */
    if (!ctx->decided) {
        return NGX_OK;
    }

    if (result == NULL) {
        st = NGX_HTTP_TRACE_ST_SUCCESS;
    } else if (ngx_strcmp(result, "error") == 0) {
        st = NGX_HTTP_TRACE_ST_ERROR;
    } else if (ngx_strcmp(result, "skipped") == 0) {
        st = NGX_HTTP_TRACE_ST_SKIPPED;
    } else if (ngx_strcmp(result, "disabled") == 0) {
        st = NGX_HTTP_TRACE_ST_DISABLED;
    } else {
        st = NGX_HTTP_TRACE_ST_SUCCESS;   /* unknown → success */
    }

    step = ngx_http_trace_add_step(r, ctx, name, "");
    if (step == NULL) {
        return NGX_OK;                  /* degrade silently (G7) */
    }

    step->status = st;
    step->type   = NGX_HTTP_TRACE_STEP_PHASE;

    if (detail != NULL && detail[0] != '\0') {
        step->note.data = (u_char *) detail;
        step->note.len  = ngx_strlen(detail);
    }

    return NGX_OK;
}
