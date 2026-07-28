/*
 * ngx_http_trace_module - upstream & gRPC capture, Layer-2 interception.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_trace_module.h"

/*
 * Log a byte range with stable markers, escaping nothing (spike: raw bytes).
 *
 * M8.6 (NFR-SEC-7): this is the one place in the module that puts raw upstream
 * bytes into the error_log, where they are outside the redaction pass and land
 * in a file with different permissions than the trace API. It is therefore
 * suppressed under `trace_hardened on`, and demoted to NGX_LOG_DEBUG otherwise
 * so a default production error_log never receives payload bytes.
 */
void
ngx_http_trace_log_bytes(ngx_http_request_t *r, const char *what,
    u_char *start, u_char *end)
{
    ngx_http_trace_main_conf_t  *mcf;

    if (start == NULL || end == NULL || end <= start) {
        return;
    }

    mcf = ngx_http_get_module_main_conf(r, ngx_http_trace_module);
    if (mcf != NULL && mcf->hardened == 1) {
        return;                         /* NFR-SEC-7: never emit payload */
    }

    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "ngx-trace: upstream-%s-bytes >>>%*s<<<",
                   what, (size_t) (end - start), start);
}

/*
 * Log the byte-exact serialized upstream request by walking u->request_bufs.
 * Guarded by request_logged so it runs at most once per request even though we
 * may reach it from two places (see below). Idempotent and side-effect-free.
 */
void
ngx_http_trace_log_request_bufs(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx)
{
    ngx_http_upstream_t  *u;
    ngx_chain_t          *cl;

    if (ctx->request_logged) {
        return;
    }

    u = r->upstream;
    if (u == NULL || u->request_bufs == NULL) {
        return;
    }

    for (cl = u->request_bufs; cl; cl = cl->next) {
        ngx_http_trace_log_bytes(r, "request", cl->buf->pos, cl->buf->last);
    }
    ctx->request_logged = 1;
}

/*
 * Is upstream capture in effect for this request? True when the request is
 * traced (ctx exists, not no_trace) AND the resolved location's
 * trace_upstream_capture is not `off` (FR-UP-7 degrade otherwise). Detection of
 * gRPC is orthogonal and handled where the try is assembled.
 */
ngx_int_t
ngx_http_trace_upstream_enabled(ngx_http_request_t *r)
{
    ngx_http_trace_ctx_t       *ctx;
    ngx_http_trace_loc_conf_t  *tlcf;

    ctx = ngx_http_trace_get_ctx(r);
    if (ctx == NULL || ctx->no_trace) {
        return 0;
    }

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_trace_module);
    if (tlcf == NULL || tlcf->upstream_capture == NGX_HTTP_TRACE_UP_OFF) {
        return 0;
    }

    return 1;
}

/*
 * Start (or return) the try currently being assembled. nginx reuses the same
 * r->upstream across retries and appends to u->state per attempt; we open a new
 * tries[] entry whenever the number of completed u->state entries has grown
 * past the tries we have already opened, so tries[] tracks attempts 1:1
 * (FR-RETRY-1). Bounded by MAX_TRIES (G3). Returns NULL on degrade.
 */
ngx_http_trace_try_t *
ngx_http_trace_try_begin(ngx_http_request_t *r, ngx_http_trace_ctx_t *ctx)
{
    ngx_http_trace_try_t  *try;

    if (ctx->cur_try != NULL) {
        return ctx->cur_try;
    }

    if (ctx->tries == NULL) {
        ctx->tries = ngx_array_create(r->pool, NGX_HTTP_TRACE_MAX_TRIES,
                                      sizeof(ngx_http_trace_try_t));
        if (ctx->tries == NULL) {
            return NULL;                        /* G7 degrade */
        }
    }

    if (ctx->tries->nelts >= NGX_HTTP_TRACE_MAX_TRIES) {
        return NULL;                            /* cap hit: degrade */
    }

    try = ngx_array_push(ctx->tries);
    if (try == NULL) {
        return NULL;
    }

    ngx_memzero(try, sizeof(ngx_http_trace_try_t));
    try->seq = ctx->tries->nelts - 1;
    try->protocol = ctx->protocol;
    try->grpc_status = -1;                      /* "no trailer captured" */
    try->connect_time = -1;
    try->response_time = -1;

    ctx->cur_try = try;

    return try;
}

/*
 * Copy up to `cap` bytes from a chain/region into a pool buffer, setting the
 * truncated flag when the source is longer. Byte-exact within the cap; the G6
 * redaction pass (ngx_http_trace_redact_ctx, M8.0) rewrites these pool copies
 * in place at LOG before anything is serialized into shm.
 */
void
ngx_http_trace_copy_capped(ngx_http_request_t *r, ngx_str_t *dst,
    u_char *src, size_t len, size_t cap, unsigned *truncated)
{
    size_t  n;

    if (src == NULL || len == 0) {
        return;
    }

    n = (len > cap) ? cap : len;

    dst->data = ngx_pnalloc(r->pool, n);
    if (dst->data == NULL) {
        return;                                 /* G7 degrade */
    }

    ngx_memcpy(dst->data, src, n);
    dst->len = n;

    if (truncated != NULL && len > cap) {
        *truncated = 1;
    }
}

/*
 * M3.1 — capture the byte-exact serialized upstream request for the current try
 * by walking u->request_bufs (the exact bytes nginx writes, FR-UP-2). Bounded
 * copy into r->pool. Idempotent per try. Also classifies the protocol as gRPC
 * when the upstream module is nginx's grpc module.
 */
void
ngx_http_trace_capture_request(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx)
{
    ngx_http_upstream_t   *u;
    ngx_http_trace_try_t  *try;
    ngx_chain_t           *cl;
    size_t                 total;
    u_char                *buf, *p, *end;

    if (!ngx_http_trace_upstream_enabled(r)) {
        return;                                 /* FR-UP-7: capture off/degrade */
    }

    u = r->upstream;
    if (u == NULL || u->request_bufs == NULL) {
        return;
    }

    try = ngx_http_trace_try_begin(r, ctx);
    if (try == NULL || try->request.len != 0) {
        return;                                 /* already captured / degrade */
    }

    /* Total serialized length across the chain. */
    total = 0;
    for (cl = u->request_bufs; cl; cl = cl->next) {
        if (cl->buf->pos && cl->buf->last > cl->buf->pos) {
            total += cl->buf->last - cl->buf->pos;
        }
    }
    if (total == 0) {
        return;
    }

    buf = ngx_pnalloc(r->pool, ngx_min(total, NGX_HTTP_TRACE_UP_REQ_MAX));
    if (buf == NULL) {
        return;
    }

    p = buf;
    end = buf + ngx_min(total, NGX_HTTP_TRACE_UP_REQ_MAX);
    for (cl = u->request_bufs; cl && p < end; cl = cl->next) {
        if (cl->buf->pos && cl->buf->last > cl->buf->pos) {
            size_t n = cl->buf->last - cl->buf->pos;
            if (n > (size_t) (end - p)) {
                n = end - p;
            }
            ngx_memcpy(p, cl->buf->pos, n);
            p += n;
        }
    }

    try->request.data = buf;
    try->request.len = p - buf;
    if (total > NGX_HTTP_TRACE_UP_REQ_MAX) {
        try->request_truncated = 1;
    }

    /*
     * M3.5 protocol detection: nginx's grpc module serializes an HTTP/2 request
     * carrying `content-type: application/grpc`. Detecting it in the byte-exact
     * captured request classifies the try (and the whole upstream) as gRPC so
     * the trailer-as-truth path (M3.6) is armed on the response.
     */
    if (ctx->protocol != NGX_HTTP_TRACE_PROTO_GRPC
        && ngx_strlcasestrn(try->request.data,
                            try->request.data + try->request.len,
                            (u_char *) "application/grpc",
                            sizeof("application/grpc") - 2) != NULL)
    {
        ctx->protocol = NGX_HTTP_TRACE_PROTO_GRPC;
        try->protocol = NGX_HTTP_TRACE_PROTO_GRPC;
    }
}

/*
 * M3.2 — snapshot the raw response header block for the current try from
 * u->buffer (byte-exact received headers, FR-UP-3) and record the parsed status
 * from u->headers_in. Called from the process_header wrap after the real parser
 * has run so headers_in.status is populated. Idempotent per try.
 */
void
ngx_http_trace_capture_response(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx)
{
    ngx_http_upstream_t   *u;
    ngx_http_trace_try_t  *try;
    size_t                 len;
    unsigned               truncated;

    if (!ngx_http_trace_upstream_enabled(r)) {
        return;                                 /* FR-UP-7: capture off/degrade */
    }

    u = r->upstream;
    if (u == NULL) {
        return;
    }

    try = ngx_http_trace_try_begin(r, ctx);
    if (try == NULL) {
        return;
    }

    if (!try->have_response && u->buffer.start != NULL
        && u->buffer.last > u->buffer.start)
    {
        len = u->buffer.last - u->buffer.start;
        truncated = 0;
        ngx_http_trace_copy_capped(r, &try->response_headers, u->buffer.start,
                                   len, NGX_HTTP_TRACE_UP_RESP_MAX,
                                   &truncated);
        try->response_truncated = truncated;
    }

    try->status = u->headers_in.status_n;
    try->have_response = 1;

    if (try->protocol == NGX_HTTP_TRACE_PROTO_GRPC) {
        ngx_http_trace_grpc_trailers(r, try);
    }
}

/*
 * M3.6 — surface gRPC trailer-as-truth. nginx's gRPC module places the trailing
 * metadata (`grpc-status`, `grpc-message`) into u->headers_in.trailers as the
 * response completes. We read them as the authoritative RPC result, distinct
 * from the HTTP :status (FR-GRPC-2). Safe to call repeatedly; the last observed
 * trailer wins.
 */
void
ngx_http_trace_grpc_trailers(ngx_http_request_t *r, ngx_http_trace_try_t *try)
{
    ngx_http_upstream_t  *u;
    ngx_list_part_t      *part;
    ngx_table_elt_t      *h;
    ngx_uint_t            i;

    u = r->upstream;
    if (u == NULL) {
        return;
    }

    part = &u->headers_in.trailers.part;
    h = part->elts;

    for (i = 0; /* void */; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            h = part->elts;
            i = 0;
        }

        if (h[i].key.len == sizeof("grpc-status") - 1
            && ngx_strncasecmp(h[i].key.data, (u_char *) "grpc-status",
                               sizeof("grpc-status") - 1) == 0)
        {
            try->grpc_status = ngx_atoi(h[i].value.data, h[i].value.len);

        } else if (h[i].key.len == sizeof("grpc-message") - 1
                   && ngx_strncasecmp(h[i].key.data, (u_char *) "grpc-message",
                                      sizeof("grpc-message") - 1) == 0)
        {
            try->grpc_message = h[i].value;
        }
    }
}

/*
 * M3.3 — harvest per-try timing/bytes/peer from the u->state array at LOG. Each
 * u->state[] entry is one attempt (FR-UP-4, FR-RETRY-1); we align it with the
 * tries[] we opened during capture, filling gaps (e.g. a connect failure that
 * produced a state entry but never reached process_header) with a
 * response-less try so retries are always visible as distinct entries.
 */
void
ngx_http_trace_harvest_state(ngx_http_request_t *r, ngx_http_trace_ctx_t *ctx)
{
    ngx_http_upstream_state_t   *state;
    ngx_http_trace_try_t        *tries, *try;
    ngx_uint_t                   i, n;

    /*
     * r->upstream_states is the ngx_array_t of ngx_http_upstream_state_t, one
     * entry per attempt (proxy_next_upstream retries append here). It survives
     * u being torn down, so it is the reliable per-try source at LOG.
     */
    if (r->upstream_states == NULL || r->upstream_states->nelts == 0) {
        return;
    }

    state = r->upstream_states->elts;
    n = r->upstream_states->nelts;
    if (n > NGX_HTTP_TRACE_MAX_TRIES) {
        n = NGX_HTTP_TRACE_MAX_TRIES;
    }

    for (i = 0; i < n; i++) {
        if (ctx->tries != NULL && i < ctx->tries->nelts) {
            tries = ctx->tries->elts;
            try = &tries[i];

        } else {
            /* A state entry without a corresponding captured try (e.g. a
               connect-time failure). Open a bare try so it still appears. */
            ctx->cur_try = NULL;
            try = ngx_http_trace_try_begin(r, ctx);
            if (try == NULL) {
                break;
            }
        }

        try->response_length = state[i].bytes_received;
        try->connect_time = (ngx_msec_int_t) state[i].connect_time;
        try->response_time = (ngx_msec_int_t) state[i].response_time;
        if (try->status == 0) {
            try->status = state[i].status;
        }
        if (state[i].peer != NULL) {
            try->peer = *state[i].peer;
        }
    }

    ctx->cur_try = NULL;
}

/*
 * Trampoline for u->create_request. Calls the real proxy create_request first
 * (which fills u->request_bufs), then logs the byte-exact serialized request.
 * Returns the original's return code unchanged so behaviour is identical
 * (FR-UP-2, FR-UP-5).
 *
 * NOTE: with proxy_pass the FIRST create_request call happens synchronously
 * inside ngx_http_upstream_init(), which the upstream content handler invokes
 * BEFORE control returns to our content-handler trampoline — so at that first
 * call u->create_request is still the original pointer and this wrap does not
 * run. It DOES run on retries/reinit. The synchronous first-call case is
 * covered by lazily logging request_bufs from the process_header wrap, which
 * reliably fires once the response header arrives. Between the two we always
 * capture the request bytes exactly once.
 */
ngx_int_t
ngx_http_trace_create_request_wrap(ngx_http_request_t *r)
{
    ngx_http_trace_ctx_t  *ctx;
    ngx_int_t              rc;

    ctx = ngx_http_trace_get_ctx(r);

    rc = ctx->orig_create_request(r);

    if (rc == NGX_OK) {
        ngx_http_trace_log_request_bufs(r, ctx);
        ngx_http_trace_capture_request(r, ctx);
    }

    return rc;
}

/*
 * Trampoline for u->process_header. Snapshots the raw u->buffer region (the
 * byte-exact response header block as received) before delegating to the real
 * proxy process_header, then returns its code unchanged (FR-UP-3, FR-UP-5).
 *
 * process_header may be called multiple times on partial reads; we log the
 * currently-buffered bytes each call — for the spike that is sufficient to
 * prove byte-exact access.
 */
ngx_int_t
ngx_http_trace_process_header_wrap(ngx_http_request_t *r)
{
    ngx_http_upstream_t   *u;
    ngx_http_trace_ctx_t  *ctx;
    u_char                *pos;
    ngx_int_t              rc;

    u = r->upstream;
    ctx = ngx_http_trace_get_ctx(r);

    /*
     * Lazily capture the request bytes here: by the time the response header
     * is being processed, u->request_bufs is populated and stable, and this
     * wrap is guaranteed to have been installed (it fired). This covers the
     * synchronous-first-create_request case described above.
     */
    ngx_http_trace_log_request_bufs(r, ctx);

    /*
     * M3.1: capture the byte-exact request NOW, before orig_process_header
     * runs. u->request_bufs is intact at this point; once the real parser runs
     * the send buffers may have their pos advanced to last (consumed), leaving
     * nothing to copy. This is the reliable capture point for the synchronous
     * first-create_request case (see note above).
     */
    ngx_http_trace_capture_request(r, ctx);

    /* snapshot the header bytes present in the buffer at this point.
     * Guarded by response_logged so we emit at most once per request
     * even when the real process_header fires on partial reads (M10.6). */
    pos = u->buffer.pos;

    if (!ctx->response_logged) {
        ngx_http_trace_log_bytes(r, "response", pos, u->buffer.last);
        ctx->response_logged = 1;
    }

    rc = ctx->orig_process_header(r);

    /*
     * M3.2: after the real parser runs, u->headers_in is populated so the
     * parsed status is available. The raw header bytes (u->buffer.start..last)
     * are untouched by the parser (it only advances pos), so snapshotting them
     * here is still byte-exact. Only finalize once the header parse completes.
     */
    if (rc == NGX_OK) {
        ngx_http_trace_capture_response(r, ctx);
    }

    return rc;
}

/*
 * Swap u->create_request / u->process_header for our trampolines, saving the
 * originals in the per-request ctx. Idempotent and defensive: if there is no
 * upstream, or the callbacks are already ours, it does nothing (FR-UP-7).
 */
void
ngx_http_trace_wrap_upstream_callbacks(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx)
{
    ngx_http_upstream_t  *u;

    u = r->upstream;
    if (u == NULL || ctx->wrapped) {
        return;
    }

    if (u->create_request == ngx_http_trace_create_request_wrap) {
        ctx->wrapped = 1;
        return;                 /* defensive: already ours */
    }

    ctx->orig_create_request = u->create_request;
    ctx->orig_process_header = u->process_header;

    u->create_request = ngx_http_trace_create_request_wrap;
    u->process_header = ngx_http_trace_process_header_wrap;
    ctx->wrapped = 1;
}

/*
 * M7.3 — resolve a handler pointer to a stable, human-meaningful name.
 *
 * nginx's content handlers (ngx_http_proxy_handler, ngx_http_static_handler,
 * ...) are `static` inside their own modules, so neither a link-time symbol
 * table nor dladdr() can name them portably. The authoritative name nginx
 * itself keeps is ngx_http_upstream_conf_t.module — set by each upstream
 * module to "proxy", "fastcgi", "grpc", "uwsgi", "scgi" or "memcached". We use
 * that when the handler created an upstream, and degrade to a generic label
 * otherwise (FR-L2-4: never guess, never fabricate).
 *
 * Returns a NUL-terminated string owned by r->pool (or a static literal).
 */
const char *
ngx_http_trace_resolve_handler_name(ngx_http_request_t *r)
{
    ngx_str_t  *m;
    u_char     *p;

    if (r->upstream != NULL && r->upstream->conf != NULL) {
        m = &r->upstream->conf->module;

        if (m->len) {
            p = ngx_pnalloc(r->pool, m->len + 1);
            if (p == NULL) {
                return "c-handler";
            }
            ngx_memcpy(p, m->data, m->len);
            p[m->len] = '\0';
            return (const char *) p;
        }
    }

    return "c-handler";
}

ngx_int_t
ngx_http_trace_content_handler_wrap(ngx_http_request_t *r)
{
    ngx_http_trace_ctx_t        *ctx;
    ngx_http_trace_main_conf_t  *mcf;
    ngx_http_trace_step_t       *step;
    ngx_int_t                    rc;
    ngx_msec_int_t               delta;
    ngx_msec_t                   start;

    ctx = ngx_http_trace_get_ctx(r);
    if (ctx == NULL || ctx->orig_content_handler == NULL) {
        /* defensive: should not happen; fall back to 500-free path */
        return NGX_DECLINED;
    }

    mcf = ngx_http_get_module_main_conf(r, ngx_http_trace_module);

    start = ngx_current_msec;

    rc = ctx->orig_content_handler(r);

    /*
     * M7.1/M7.2 — Layer 2 named CONTENT step. Only when the version gate
     * armed interception (mcf->intercept_active) and this request is traced.
     * The step records the resolved handler name and how long the call took.
     *
     * `rc` is returned verbatim below — including NGX_AGAIN/NGX_DONE, which
     * suspend the request. nginx's content phase invokes r->content_handler
     * exactly once (ngx_http_core_content_phase returns after the call and
     * resumption happens through upstream event handlers, not by re-entering
     * this trampoline), so the step can never be double-counted (AC-16).
     */
    if (mcf != NULL && mcf->intercept_active && !ctx->no_trace) {
        step = ngx_http_trace_add_step(r, ctx, "CONTENT",
                                       ngx_http_trace_resolve_handler_name(r));
        if (step != NULL) {
            /*
             * t_offset_us stays as add_step computed it (offset from request
             * start, consistent with every Layer-1 step). The call duration is
             * recorded separately so the timeline keeps one time base.
             */
            delta = (ngx_msec_int_t) (ngx_current_msec - start);
            if (delta < 0) {
                delta = 0;
            }
            step->duration_us = (ngx_uint_t) delta * 1000;
            step->timed = 1;
            step->status = (rc == NGX_ERROR
                            || rc == (ngx_int_t) NGX_HTTP_INTERNAL_SERVER_ERROR)
                           ? NGX_HTTP_TRACE_ST_ERROR
                           : NGX_HTTP_TRACE_ST_SUCCESS;
        }
    }

    /*
     * At this point proxy's handler has created r->upstream, called
     * ngx_http_upstream_init() (which invokes the ORIGINAL create_request
     * synchronously, populating u->request_bufs), and started the async
     * connect (rc == NGX_AGAIN/NGX_DONE). u->request_bufs is still intact
     * here, so capture the byte-exact request now. Then wrap the callbacks so
     * process_header (fired later on the response) is also observed, and so
     * any retry's create_request goes through our wrap too.
     */
    if (r->upstream != NULL) {
        ngx_http_trace_log_request_bufs(r, ctx);
        ngx_http_trace_capture_request(r, ctx);
    }
    ngx_http_trace_wrap_upstream_callbacks(r, ctx);

    return rc;
}

/*
 * PRECONTENT-phase installer. Runs before the CONTENT phase. If this request has
 * a content handler set (proxy_pass et al. set r->content_handler during
 * FIND_CONFIG/rewrite), we save it and replace it with our trampoline so we gain
 * control around upstream creation. Purely additive: if there is no content
 * handler, or allocation fails, we decline and change nothing (FR-UP-7 degrade).
 */
ngx_int_t
ngx_http_trace_precontent_handler(ngx_http_request_t *r)
{
    ngx_http_trace_ctx_t  *ctx;

    if (r != r->main) {
        return NGX_DECLINED;         /* skip subrequests for the spike */
    }

    if (r->content_handler == NULL) {
        return NGX_DECLINED;         /* not a content-handler location */
    }

    if (r->content_handler == ngx_http_trace_content_handler_wrap) {
        return NGX_DECLINED;         /* already wrapped */
    }

    /*
     * Reuse the per-request ctx created by the POST_READ selector (single ctx
     * slot per module). If the selector declined to trace this request there is
     * still a ctx (with no_trace set) — we save the callbacks into it so the
     * byte-exact capture spike keeps working independent of the trace decision.
     * If somehow there is no ctx (selector never ran), decline and change
     * nothing (FR-UP-7 degrade).
     */
    ctx = ngx_http_trace_get_ctx(r);
    if (ctx == NULL) {
        return NGX_DECLINED;
    }

    ctx->orig_content_handler = r->content_handler;

    r->content_handler = ngx_http_trace_content_handler_wrap;

    return NGX_DECLINED;
}
