/*
 * ngx_http_trace_module - JSON serialization & transaction commit.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_trace_module.h"

/* Map an ST_* status to its schema label (FR-STATUS-1). */
const char *
ngx_http_trace_status_str(ngx_uint_t status)
{
    switch (status) {
    case NGX_HTTP_TRACE_ST_ERROR:    return "error";
    case NGX_HTTP_TRACE_ST_SKIPPED:  return "skipped";
    case NGX_HTTP_TRACE_ST_DISABLED: return "disabled";
    default:                         return "success";
    }
}

/* Map an ES_* error_state to its schema label (FR-FAULT-1, M4.1). */
const char *
ngx_http_trace_error_state_str(ngx_uint_t es)
{
    switch (es) {
    case NGX_HTTP_TRACE_ES_ACCESS_DENIED:  return "access_denied";
    case NGX_HTTP_TRACE_ES_NOT_FOUND:      return "not_found";
    case NGX_HTTP_TRACE_ES_CLIENT_ERROR:   return "client_error";
    case NGX_HTTP_TRACE_ES_UPSTREAM_ERROR: return "upstream_error";
    case NGX_HTTP_TRACE_ES_SERVER_ERROR:   return "server_error";
    default:                               return "none";
    }
}

/* Map a var op to its schema label (FR-VAR-2). */
const char *
ngx_http_trace_op_str(ngx_uint_t op)
{
    switch (op) {
    case NGX_HTTP_TRACE_OP_SET:         return "set";
    case NGX_HTTP_TRACE_OP_SET_FAILED:  return "set_failed";
    default:                            return "read";
    }
}

/*
 * Append a JSON string literal, escaping the characters JSON requires (", \\,
 * and control chars). Bounded by `last`; returns the new write position. Used
 * for values that can contain arbitrary bytes (variable values, URIs).
 */
u_char *
ngx_http_trace_json_str(u_char *p, u_char *last, ngx_str_t *s)
{
    ngx_uint_t  i;
    u_char      c;

    if (p >= last) {
        return p;
    }
    *p++ = '"';

    for (i = 0; s != NULL && i < s->len && p < last; i++) {
        c = s->data[i];
        switch (c) {
        case '"':  case '\\':
            if (last - p < 2) { goto done; }
            *p++ = '\\'; *p++ = c;
            break;
        case '\n':
            if (last - p < 2) { goto done; }
            *p++ = '\\'; *p++ = 'n';
            break;
        case '\r':
            if (last - p < 2) { goto done; }
            *p++ = '\\'; *p++ = 'r';
            break;
        case '\t':
            if (last - p < 2) { goto done; }
            *p++ = '\\'; *p++ = 't';
            break;
        default:
            if (c < 0x20) {
                if (last - p < 6) { goto done; }
                p = ngx_snprintf(p, last - p, "\\u%04xd", (ngx_uint_t) c);
            } else if (c >= 0x7f) {
                /* Escape non-ASCII high bytes as \u00XX to guarantee valid UTF-8 JSON */
                if (last - p < 6) { goto done; }
                p = ngx_snprintf(p, last - p, "\\u00%02xd", (ngx_uint_t) c);
            } else {
                *p++ = c;
            }
        }
    }

done:
    if (p < last) {
        *p++ = '"';
    }
    return p;
}

/*
 * Serialize one step's watch-list snapshot as a JSON object keyed by var name,
 * each value an object {value, op}. Empty when the step has no snapshot.
 */
u_char *
ngx_http_trace_json_vars(u_char *p, u_char *last, ngx_http_trace_step_t *step)
{
    ngx_http_trace_var_t  *vars;
    ngx_uint_t             i;

    if (step->vars == NULL || step->vars->nelts == 0) {
        return ngx_snprintf(p, last - p, "{}");
    }

    vars = step->vars->elts;

    p = ngx_snprintf(p, last - p, "{");
    for (i = 0; i < step->vars->nelts; i++) {
        if (i) {
            p = ngx_snprintf(p, last - p, ",");
        }
        p = ngx_http_trace_json_str(p, last, &vars[i].name);
        p = ngx_snprintf(p, last - p, ":{\"value\":");
        p = ngx_http_trace_json_str(p, last, &vars[i].value);
        p = ngx_snprintf(p, last - p, ",\"op\":\"%s\"}",
                         ngx_http_trace_op_str(vars[i].op));
    }
    p = ngx_snprintf(p, last - p, "}");

    return p;
}

/*
 * M3 — serialize the upstream section (schema §8.3 upstream). Emitted only when
 * the request went upstream (tries were captured). Each try carries the
 * byte-exact sent request and raw received response headers (capped, with
 * truncation flags), the parsed HTTP status, u->state timing/bytes, and — for
 * gRPC — the trailer-sourced grpc-status/message as the authoritative result
 * (FR-UP-*, FR-RETRY-1, FR-GRPC-2, FR-JSON-3). Returns p unchanged (nothing
 * emitted) when there is no upstream data, so the caller's JSON stays valid.
 */
u_char *
ngx_http_trace_json_upstream(u_char *p, u_char *last, ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx)
{
    ngx_http_trace_try_t  *tries, *try;
    ngx_uint_t             i;

    if (ctx->tries == NULL || ctx->tries->nelts == 0) {
        return p;                       /* no upstream: omit section */
    }

    tries = ctx->tries->elts;

    p = ngx_snprintf(p, last - p, ",\"upstream\":{\"protocol\":\"%s\",\"tries\":[",
                     ctx->protocol == NGX_HTTP_TRACE_PROTO_GRPC
                        ? "grpc" : "http");

    for (i = 0; i < ctx->tries->nelts; i++) {
        try = &tries[i];

        if (i) {
            p = ngx_snprintf(p, last - p, ",");
        }

        p = ngx_snprintf(p, last - p, "{\"seq\":%ui,\"peer\":", try->seq);
        p = ngx_http_trace_json_str(p, last, &try->peer);

        p = ngx_snprintf(p, last - p, ",\"status\":%ui,\"bytes\":%O",
                         try->status, try->response_length);

        if (try->connect_time >= 0) {
            p = ngx_snprintf(p, last - p, ",\"connect_ms\":%i",
                             (ngx_int_t) try->connect_time);
        }
        if (try->response_time >= 0) {
            p = ngx_snprintf(p, last - p, ",\"response_ms\":%i",
                             (ngx_int_t) try->response_time);
        }

        p = ngx_snprintf(p, last - p, ",\"request\":");
        p = ngx_http_trace_json_str(p, last, &try->request);
        p = ngx_snprintf(p, last - p, ",\"request_truncated\":%s",
                         try->request_truncated ? "true" : "false");

        p = ngx_snprintf(p, last - p, ",\"response_headers\":");
        p = ngx_http_trace_json_str(p, last, &try->response_headers);
        p = ngx_snprintf(p, last - p, ",\"response_truncated\":%s",
                         try->response_truncated ? "true" : "false");

        if (try->protocol == NGX_HTTP_TRACE_PROTO_GRPC) {
            if (try->grpc_status >= 0) {
                p = ngx_snprintf(p, last - p, ",\"grpc_status\":%i",
                                 (ngx_int_t) try->grpc_status);
            } else {
                p = ngx_snprintf(p, last - p, ",\"grpc_status\":null");
            }
            if (try->grpc_message.len) {
                p = ngx_snprintf(p, last - p, ",\"grpc_message\":");
                p = ngx_http_trace_json_str(p, last, &try->grpc_message);
            }
        }

        p = ngx_snprintf(p, last - p, "}");
    }

    p = ngx_snprintf(p, last - p, "]}");

    return p;
}

/*
 * M4.1 — emit the summary.fault object (schema §8.3). Emitted only when a fault
 * was detected; carries phase, handler, code, status, error_state, message and
 * step_seq (the exact linked step). Returns p unchanged when there is no fault
 * so the caller's JSON stays valid.
 */
u_char *
ngx_http_trace_json_fault(u_char *p, u_char *last, ngx_http_trace_ctx_t *ctx)
{
    ngx_http_trace_fault_t  *f = &ctx->fault;

    if (!f->have) {
        return p;                       /* no fault: omit section */
    }

    p = ngx_snprintf(p, last - p, ",\"fault\":{\"phase\":");
    p = ngx_http_trace_json_str(p, last, &f->phase);
    p = ngx_snprintf(p, last - p, ",\"handler\":");
    p = ngx_http_trace_json_str(p, last, &f->handler);
    p = ngx_snprintf(p, last - p,
                     ",\"code\":%ui,\"status\":%ui,\"error_state\":\"%s\"",
                     f->code, f->status,
                     ngx_http_trace_error_state_str(f->error_state));
    p = ngx_snprintf(p, last - p, ",\"message\":");
    p = ngx_http_trace_json_str(p, last, &f->message);
    p = ngx_snprintf(p, last - p, ",\"step_seq\":%i}", f->step_seq);

    return p;
}

/*
 * M2.7 — commit the captured transaction (real Layer-1 timeline) into the shm
 * ring slot exactly once (G5). Called from the LOG phase — the request is
 * complete, so method/uri/status and every step are final. Serializes the
 * append-only step list to JSON with per-step {phase, handler, t_offset_us,
 * status, type, note, vars}. Bounded and defensive: never fails the request,
 * silently degrades if the zone is absent, already committed, or the JSON would
 * overflow the slot (G7).
 */
void
ngx_http_trace_commit(ngx_http_request_t *r)
{
    ngx_http_trace_main_conf_t  *mcf;
    ngx_http_trace_shctx_t      *shctx;
    ngx_http_trace_slot_t       *slot;
    ngx_http_trace_ctx_t        *ctx;
    ngx_http_trace_step_t       *steps;
    u_char                       buf[NGX_HTTP_TRACE_SLOT_MAX];
    u_char                      *p, *last;
    ngx_uint_t                   status, i, nsteps, cap, txn_seq;

    ctx = ngx_http_trace_get_ctx(r);
    if (ctx == NULL || ctx->no_trace) {
        return;                         /* not traced: nothing to commit */
    }

    if (ctx->committed) {
        return;                         /* G5: commit exactly once */
    }

    /*
     * M4.3 — fault-only sessions (FR-SEL-4). The request was recorded
     * provisionally in the pool ctx; commit only if it finalized as a fault
     * (optionally matching a specific code). Otherwise discard: mark committed
     * so we never retry, and leave the ring buffer untouched (AC-10).
     */
    {
        ngx_http_trace_loc_conf_t  *tlcf;

        tlcf = ngx_http_get_module_loc_conf(r, ngx_http_trace_module);
        if (tlcf != NULL && tlcf->fault_only) {
            if (!ctx->fault.have
                || (tlcf->fault_code != 0
                    && ctx->fault.status != tlcf->fault_code))
            {
                ctx->committed = 1;     /* discard provisional record */
                return;
            }
        }
    }

    mcf = ngx_http_get_module_main_conf(r, ngx_http_trace_module);
    if (mcf == NULL || mcf->shm_zone == NULL) {
        return;                         /* no zone configured: nothing to do */
    }

    shctx = mcf->shm_zone->data;
    if (shctx == NULL || shctx->ring == NULL) {
        return;
    }

    status = r->headers_out.status ? r->headers_out.status : r->err_status;

    /*
     * M3.3 — harvest per-try timing/bytes/peer from u->state now (at LOG the
     * upstream is complete and u->states is final). Gated by upstream_enabled
     * so `trace_upstream_capture off` degrades to no upstream section.
     */
    if (ngx_http_trace_upstream_enabled(r)) {
        ngx_http_trace_harvest_state(r, ctx);
    }

    /*
     * M8.1 — snapshot the client request body. Done here, at LOG, because any
     * consumer that was going to read it already has, so we never force a read
     * (FR-BODY-2) and never block (FR-BODY-3).
     */
    ngx_http_trace_capture_request_body(r, ctx);

    /*
     * M8.2 — response content_type/content_encoding. Read here rather than in
     * our header filter because we sit at the head of the output chain, so at
     * header-filter time gzip has not yet set content_encoding.
     */
    ngx_http_trace_capture_response_meta(r, ctx);

    /*
     * M8.0 — the redaction pass (G6 / NFR-SEC-2/3/8, AC-11). This MUST be the
     * last thing that touches captured bytes before serialization: everything
     * below writes into `buf` and then into shm, so a secret not masked here is
     * a secret that reaches shared memory.
     */
    ngx_http_trace_redact_ctx(r, ctx);

    steps  = (ctx->steps != NULL) ? ctx->steps->elts : NULL;
    nsteps = (ctx->steps != NULL) ? ctx->steps->nelts : 0;

    p = buf;
    last = buf + sizeof(buf);

    p = ngx_snprintf(p, last - p,
                     "{\"txn\":\"trace\",\"method\":");
    p = ngx_http_trace_json_str(p, last, &r->method_name);
    p = ngx_snprintf(p, last - p, ",\"uri\":");
    p = ngx_http_trace_json_str(p, last, &r->uri);
    p = ngx_snprintf(p, last - p,
                     ",\"worker_pid\":%P,\"connection_id\":%ud,"
                     "\"status\":%ui,\"steps\":[",
                     ngx_pid, r->connection->number, status);

    for (i = 0; i < nsteps; i++) {
        if (i) {
            p = ngx_snprintf(p, last - p, ",");
        }
        p = ngx_snprintf(p, last - p,
                         "{\"seq\":%ui,\"phase\":", steps[i].seq);
        p = ngx_http_trace_json_str(p, last, &steps[i].phase);
        p = ngx_snprintf(p, last - p, ",\"handler\":");
        p = ngx_http_trace_json_str(p, last, &steps[i].handler);
        p = ngx_snprintf(p, last - p,
                         ",\"t_offset_us\":%i,\"status\":\"%s\",\"type\":\"%s\"",
                         (ngx_int_t) steps[i].t_offset_us,
                         ngx_http_trace_status_str(steps[i].status),
                         steps[i].type == NGX_HTTP_TRACE_STEP_CONDITION
                            ? "condition"
                            : (steps[i].type == NGX_HTTP_TRACE_STEP_SUBREQUEST
                                  ? "subrequest" : "phase"));
        if (steps[i].type == NGX_HTTP_TRACE_STEP_CONDITION) {
            p = ngx_snprintf(p, last - p, ",\"evaluated\":%s",
                             steps[i].evaluated ? "true" : "false");
        }
        /* M7.1: Layer-2 named steps carry the measured handler duration. */
        if (steps[i].timed) {
            p = ngx_snprintf(p, last - p, ",\"duration_us\":%i",
                             (ngx_int_t) steps[i].duration_us);
        }
        if (steps[i].note.len) {
            p = ngx_snprintf(p, last - p, ",\"note\":");
            p = ngx_http_trace_json_str(p, last, &steps[i].note);
        }
        p = ngx_snprintf(p, last - p, ",\"vars\":");
        p = ngx_http_trace_json_vars(p, last, &steps[i]);
        p = ngx_snprintf(p, last - p, "}");
    }

    p = ngx_snprintf(p, last - p, "]");

    /*
     * M3: upstream section (byte-exact per-try request/response, retries, and
     * gRPC trailer-as-truth). Emitted only when this request went upstream and
     * capture is enabled; otherwise omitted (FR-UP-7 degrade).
     */
    p = ngx_http_trace_json_upstream(p, last, r, ctx);

    /*
     * M4.1: summary.fault (phase/handler/code/status/error_state/message/
     * step_seq). Emitted only when a fault was detected; omitted otherwise.
     */
    p = ngx_http_trace_json_fault(p, last, ctx);

    /*
     * M8.5 — body previews (FR-BODY-5). Each side is omitted entirely when
     * nothing was captured, so transactions from locations with body capture off
     * are byte-identical to their pre-M8 form. The request side carries no
     * content_type because the *request* Content-Type is already visible in the
     * captured upstream request headers.
     */
    p = ngx_http_trace_json_body(p, last, "request_body", ctx->req_body,
                                 ctx->req_body_len, ctx->req_body_total,
                                 ctx->req_body_truncated, ctx->req_body_binary,
                                 NULL, NULL);

    p = ngx_http_trace_json_body(p, last, "response_body", ctx->resp_body,
                                 ctx->resp_body_len, ctx->resp_body_total,
                                 ctx->resp_body_truncated,
                                 ctx->resp_body_binary,
                                 &ctx->resp_content_type,
                                 &ctx->resp_content_encoding);

    /*
     * M5.2/M5.3/M5.5 — commit into the bounded ring buffer.
     *
     * The mutex is acquired BEFORE the overflow check so the dropped counter
     * increment is atomic across workers (M10.1 / REVIEW.md #4). The lock is
     * held through the rest of the commit — lazy retention, per-session
     * bookkeeping, ring-slot copy — and released once the slot is committed.
     * Serialization happens outside the lock (M5.5).
     */
    if (shctx != NULL) {
        ngx_shmtx_lock(&shctx->shpool->mutex);
    }

    p = ngx_snprintf(p, last - p, "}");

    if ((size_t) (p - buf) >= sizeof(buf)) {
        /*
         * Would overflow the slot: mark committed so we do not retry,
         * bump the dropped counter under the mutex (M10.1: REVIEW.md #4),
         * and degrade gracefully (G7).
         */
        ctx->committed = 1;
        if (shctx != NULL) {
            shctx->dropped++;
        }
        ngx_shmtx_unlock(&shctx->shpool->mutex);
        ngx_http_trace_diag(mcf, NGX_HTTP_TRACE_LOG_WARN, r->connection->log,
                            "commit skipped: txn exceeds slot (%uz bytes)",
                            (size_t) (p - buf));
        return;
    }

    ngx_http_trace_expire_locked(shctx, ngx_time());

    /*
     * M6.2 — per-session bookkeeping. If this request is bound to a live
     * capturing session, assign the next per-session txn index and enforce the
     * session's own transaction cap: once `captured` reaches max, the session
     * auto-stops (stopped_reason=max_reached) and this transaction is dropped.
     */
    {
        ngx_http_trace_session_t  *sess = NULL;

        if (ctx->session_id != 0) {
            sess = ngx_http_trace_session_find_locked(shctx, ctx->session_id);
            if (sess == NULL
                || sess->state != NGX_HTTP_TRACE_SESS_CAPTURING)
            {
                sess = NULL;                /* session gone/stopped: ring-only */
            }
        }

        if (sess != NULL) {
            if (sess->captured >= sess->max_transactions) {
                sess->state = NGX_HTTP_TRACE_SESS_STOPPED;
                sess->stopped_reason = NGX_HTTP_TRACE_STOP_MAX_REACHED;
                ngx_shmtx_unlock(&shctx->shpool->mutex);
                ctx->committed = 1;         /* session full: drop (M5.3) */
                return;
            }
            txn_seq = ++sess->captured;
        } else {
            txn_seq = 0;                    /* ring-only: fall back to slot->seq */
        }
    }

    slot = &shctx->ring[shctx->head];

    ngx_memcpy(slot->json, buf, p - buf);
    slot->len        = p - buf;
    slot->committed  = 1;
    slot->seq        = ++shctx->seq;
    slot->session_id = ctx->session_id;
    slot->txn_seq    = txn_seq ? txn_seq : slot->seq;
    slot->started_at = ctx->start_time;
    slot->ended_at   = ngx_time();
    slot->status     = status;
    slot->duration_us = (ngx_uint_t) (
        (uint64_t) (ngx_current_msec - ctx->start_msec) * 1000);
    slot->has_fault   = ctx->fault.have ? 1 : 0;
    slot->fault_code  = ctx->fault.have ? ctx->fault.status : 0;

    /* M9 — correlation keys always populated (FR-EBPF-2). */
    slot->worker_pid    = ngx_pid;
    slot->connection_id = r->connection->number;

    slot->method_len = ngx_min(r->method_name.len, sizeof(slot->method));
    ngx_memcpy(slot->method, r->method_name.data, slot->method_len);
    slot->path_len = ngx_min(r->uri.len, (size_t) NGX_HTTP_TRACE_SUMM_MAX);
    ngx_memcpy(slot->path, r->uri.data, slot->path_len);

    shctx->head = (shctx->head + 1) % NGX_HTTP_TRACE_RING_SLOTS;
    if (shctx->count < NGX_HTTP_TRACE_RING_SLOTS) {
        shctx->count++;
    }

    /*
     * M5.3 — global transaction cap (trace_max_transactions). The physical ring
     * is RING_SLOTS deep; if the operator caps below that, keep only the most
     * recent `cap` transactions visible by trimming `count`. Oldest entries fall
     * out of the read window (they will be physically overwritten in due course).
     */
    cap = (mcf->max_transactions < NGX_HTTP_TRACE_RING_SLOTS)
              ? mcf->max_transactions : NGX_HTTP_TRACE_RING_SLOTS;
    if (cap == 0) {
        cap = 1;
    }
    if (shctx->count > cap) {
        shctx->count = cap;
    }

    ngx_shmtx_unlock(&shctx->shpool->mutex);

    ctx->committed = 1;                  /* G5: exactly once */

    /*
     * FR-LOG-5: per-request commit milestone. Level-gated so it costs nothing
     * unless trace_log_level >= debug. Carries only metadata (status + size),
     * never payload bytes (FR-LOG-6).
     */
    ngx_http_trace_diag(mcf, NGX_HTTP_TRACE_LOG_DEBUG, r->connection->log,
                        "commit txn status=%ui bytes=%uz steps=%ui", status,
                        (size_t) (p - buf), nsteps);
}
