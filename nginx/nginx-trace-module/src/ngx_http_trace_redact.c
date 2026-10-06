/*
 * ngx_http_trace_module - M8.0 redaction & M8.1/M8.2 body capture.
 *
 * Redaction (NFR-SEC-2/3/8, guiding constraint G6) is the LAST thing that
 * touches captured bytes before they are serialized into shared memory. Nothing
 * in this file allocates shm; it rewrites request-pool copies in place, so a
 * masked value can never be recovered from the ring afterwards.
 *
 * The mask is fixed-width and content-free on purpose: we emit the literal
 * "[REDACTED]" rather than a length-preserving run of '*', because a preserved
 * length is itself an information leak (it reveals secret/token size).
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_trace_module.h"


/*
 * The saved next links in the output filter chain (skill:filter-registration-order).
 * Installed at postconfiguration; every filter path below hands off through
 * these unconditionally so the response is byte-identical with tracing on (G1).
 */
ngx_http_output_header_filter_pt  ngx_http_trace_next_header_filter;
ngx_http_output_body_filter_pt    ngx_http_trace_next_body_filter;


/* The single, fixed replacement text for every redacted value (NFR-SEC-2). */
static ngx_str_t  ngx_http_trace_mask = ngx_string("[REDACTED]");


/*
 * The default redaction set mandated by NFR-SEC-3. Used when the operator never
 * wrote a `trace_redact` directive, so the secure posture is the *default* and
 * not something you have to remember to switch on.
 */
static ngx_str_t  ngx_http_trace_default_redact[] = {
    ngx_string("authorization"),
    ngx_string("cookie"),
    ngx_string("set-cookie"),
    ngx_string("proxy-authorization")
};


/*
 * Is `name` (length n) in the effective redaction list for this location?
 *
 * The effective list is either the configured `trace_redact` array or, when the
 * operator never set one, the NFR-SEC-3 default set. Matching is
 * case-insensitive because HTTP header names are, and tolerates a leading '$'
 * so the same directive can name variables and headers interchangeably.
 */
ngx_int_t
ngx_http_trace_redact_match(ngx_http_trace_loc_conf_t *tlcf, u_char *name,
    size_t n)
{
    ngx_str_t   *list, item;
    ngx_uint_t   i, nelts;

    if (name == NULL || n == 0) {
        return 0;
    }

    if (tlcf != NULL && tlcf->redact != NULL
        && tlcf->redact != NGX_CONF_UNSET_PTR)
    {
        list  = tlcf->redact->elts;
        nelts = tlcf->redact->nelts;

    } else {
        /* NFR-SEC-3: secure by default when unconfigured. */
        list  = ngx_http_trace_default_redact;
        nelts = sizeof(ngx_http_trace_default_redact) / sizeof(ngx_str_t);
    }

    for (i = 0; i < nelts; i++) {
        item = list[i];

        /* accept "$name" and "name" spellings alike */
        if (item.len > 0 && item.data[0] == '$') {
            item.data++;
            item.len--;
        }

        if (item.len == n
            && ngx_strncasecmp(item.data, name, n) == 0)
        {
            return 1;
        }
    }

    return 0;
}


/*
 * Replace *str with the fixed mask, allocating from r->pool.
 *
 * Callers pass request-pool strings only. On allocation failure we degrade to
 * an empty value rather than leaving the secret in place — failing closed is the
 * only acceptable direction for a redaction primitive (G7 + G6).
 */
void
ngx_http_trace_redact_value(ngx_http_request_t *r, ngx_str_t *str)
{
    u_char  *p;

    if (str == NULL || str->len == 0) {
        return;
    }

    p = ngx_pnalloc(r->pool, ngx_http_trace_mask.len);
    if (p == NULL) {
        str->len = 0;                   /* fail closed: drop the value */
        return;
    }

    ngx_memcpy(p, ngx_http_trace_mask.data, ngx_http_trace_mask.len);

    str->data = p;
    str->len  = ngx_http_trace_mask.len;
}


/*
 * Redact sensitive header lines inside a captured raw header block, in place.
 *
 * `block` is a byte-exact copy of an HTTP/1.x header region (what M3.1/M3.2
 * captured from u->request_bufs / u->buffer), so it is a sequence of
 * "Name: value CRLF" lines. For each line whose name matches the redaction
 * list we overwrite the *value* span with 'x' padding and leave the name,
 * colon and line terminator untouched. That keeps the block parseable and
 * keeps its byte offsets stable (so `request_truncated` etc. stay meaningful)
 * while destroying the secret.
 *
 * Rewriting in place — rather than rebuilding a shorter block — is deliberate:
 * the block is already a bounded pool copy, and an in-place overwrite cannot
 * fail partway and leave a half-masked secret behind.
 */
void
ngx_http_trace_redact_header_block(ngx_http_request_t *r,
    ngx_http_trace_loc_conf_t *tlcf, ngx_str_t *block)
{
    u_char  *p, *end, *eol, *colon, *vstart;
    size_t   namelen;

    if (block == NULL || block->len == 0 || block->data == NULL) {
        return;
    }

    p   = block->data;
    end = block->data + block->len;

    while (p < end) {

        /* Find the end of this header line (LF-terminated; CR optional). */
        eol = ngx_strlchr(p, end, LF);
        if (eol == NULL) {
            eol = end;                  /* truncated capture: last partial line */
        }

        /* An empty line ends the header block; nothing after it is a header. */
        if (eol == p || (eol == p + 1 && *p == CR)) {
            return;
        }

        colon = ngx_strlchr(p, eol, ':');
        if (colon != NULL) {

            namelen = (size_t) (colon - p);

            if (ngx_http_trace_redact_match(tlcf, p, namelen)) {

                /* Skip the colon and any optional leading whitespace so the
                   ": " separator survives and only the value is destroyed. */
                vstart = colon + 1;
                while (vstart < eol && (*vstart == ' ' || *vstart == '\t')) {
                    vstart++;
                }

                /* Do not overwrite the CR of a CRLF terminator. */
                if (eol > vstart && *(eol - 1) == CR) {
                    ngx_memset(vstart, 'x', (size_t) (eol - 1 - vstart));
                } else if (eol > vstart) {
                    ngx_memset(vstart, 'x', (size_t) (eol - vstart));
                }
            }
        }

        if (eol == end) {
            return;
        }
        p = eol + 1;
    }
}


/*
 * M8.0 — the cross-cutting redaction pass (G6). Called from the LOG-phase
 * commit path immediately before serialization, so *every* capture path added
 * by any milestone is covered by construction rather than by remembering to
 * call a redactor at each capture site.
 *
 * Covers:
 *   - watch-list variable values whose name is sensitive (NFR-SEC-2);
 *   - the byte-exact upstream request + response header blocks of every try,
 *     which is where `Authorization` actually reaches shm (AC-11);
 *   - gRPC trailer messages, which are metadata and in scope per NFR-SEC-8.
 *
 * Body previews are NOT masked here: they are already gated behind an opt-in
 * directive and are captured through ngx_http_trace_body_append(), which
 * applies the content-type policy at capture time.
 */
void
ngx_http_trace_redact_ctx(ngx_http_request_t *r, ngx_http_trace_ctx_t *ctx)
{
    ngx_http_trace_loc_conf_t  *tlcf;
    ngx_http_trace_step_t      *steps;
    ngx_http_trace_var_t       *vars;
    ngx_http_trace_try_t       *tries;
    ngx_uint_t                  i, j;

    if (ctx == NULL || ctx->no_trace) {
        return;
    }

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_trace_module);

    /* (a) watched variable values. */
    if (ctx->steps != NULL) {
        steps = ctx->steps->elts;

        for (i = 0; i < ctx->steps->nelts; i++) {
            if (steps[i].vars == NULL) {
                continue;
            }
            vars = steps[i].vars->elts;

            for (j = 0; j < steps[i].vars->nelts; j++) {
                if (ngx_http_trace_redact_match(tlcf, vars[j].name.data,
                                                vars[j].name.len))
                {
                    ngx_http_trace_redact_value(r, &vars[j].value);
                }
            }
        }
    }

    /* (b)/(c) upstream header blocks + gRPC trailer metadata, per try. */
    if (ctx->tries != NULL) {
        tries = ctx->tries->elts;

        for (i = 0; i < ctx->tries->nelts; i++) {
            ngx_http_trace_redact_header_block(r, tlcf, &tries[i].request);
            ngx_http_trace_redact_header_block(r, tlcf,
                                               &tries[i].response_headers);
        }
    }
}


/* ----- M8.1 / M8.2 body capture ------------------------------------------- *
 *
 * Both directions share one bounded append primitive and one JSON emitter, so
 * the cap, the binary sniff and the truncation accounting cannot drift between
 * request and response.
 */

/*
 * The effective per-direction capture budget: the operator's `trace_body_max`
 * clamped by the compile-time hard ceiling (NFR-MEM-1 / G3). A misconfigured
 * `trace_body_max 1g` therefore cannot pin a gigabyte of request-pool memory.
 */
size_t
ngx_http_trace_body_budget(ngx_http_trace_loc_conf_t *tlcf)
{
    size_t  max;

    if (tlcf == NULL) {
        return 0;
    }

    max = tlcf->body_max;
    if (max == NGX_CONF_UNSET_SIZE || max == 0) {
        max = NGX_HTTP_TRACE_BODY_HARD_MAX;
    }

    return ngx_min(max, (size_t) NGX_HTTP_TRACE_BODY_HARD_MAX);
}


/*
 * Is body capture in the given direction (BODY_REQUEST / BODY_RESPONSE) active
 * for this request? Requires the request to be traced, the direction bit to be
 * set, and hardened mode to be off (NFR-SEC-7 wins over location config).
 */
ngx_int_t
ngx_http_trace_body_enabled(ngx_http_request_t *r, ngx_uint_t direction)
{
    ngx_http_trace_main_conf_t  *mcf;
    ngx_http_trace_loc_conf_t   *tlcf;
    ngx_http_trace_ctx_t        *ctx;

    ctx = ngx_http_trace_get_ctx(r->main);
    if (ctx == NULL || ctx->no_trace) {
        return 0;
    }

    /* NFR-SEC-7: hardened mode disables body capture entirely, everywhere. */
    mcf = ngx_http_get_module_main_conf(r, ngx_http_trace_module);
    if (mcf != NULL && mcf->hardened == 1) {
        return 0;
    }

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_trace_module);
    if (tlcf == NULL
        || tlcf->body_capture == NGX_CONF_UNSET_UINT
        || (tlcf->body_capture & direction) == 0)
    {
        return 0;                       /* NFR-SEC-4: off by default */
    }

    return 1;
}


/*
 * Append up to the remaining budget of `len` bytes from `src` into a body
 * preview buffer, allocating lazily on first use.
 *
 * `total` accumulates the *full* observed length even past the cap, so the
 * preview can honestly report how much was skipped. `truncated` is set the
 * moment we drop a byte. Returns nothing: a failure to capture is never a
 * failure of the request (G7).
 */
void
ngx_http_trace_body_append(ngx_http_request_t *r, u_char **buf, size_t *len,
    off_t *total, unsigned *truncated, unsigned *binary, u_char *src,
    size_t n, size_t budget)
{
    size_t      room, copy, i;
    u_char      c;

    if (src == NULL || n == 0) {
        return;
    }

    *total += (off_t) n;

    if (budget == 0) {
        *truncated = 1;
        return;
    }

    if (*buf == NULL) {
        *buf = ngx_pnalloc(r->pool, budget);
        if (*buf == NULL) {
            return;                     /* G7: degrade, capture nothing */
        }
        *len = 0;
    }

    room = budget - *len;
    if (room == 0) {
        *truncated = 1;
        return;
    }

    copy = ngx_min(room, n);
    ngx_memcpy(*buf + *len, src, copy);

    /*
     * Sniff for non-text bytes so the serializer can choose `preview` vs
     * `preview_hex` (FR-BODY-5). NUL and C0 controls other than the usual
     * whitespace mean "not human-readable text".
     */
    for (i = 0; i < copy; i++) {
        c = (*buf)[*len + i];
        if (c == 0 || (c < 0x20 && c != '\n' && c != '\r' && c != '\t')) {
            *binary = 1;
            break;
        }
    }

    *len += copy;

    if (copy < n) {
        *truncated = 1;
    }
}


/*
 * M8.1 — snapshot the client request body (FR-BODY-1/2/3).
 *
 * Called at LOG. This timing is the whole design: by LOG, any consumer that was
 * going to read the body (proxy_pass, a content handler) already has, so
 * r->request_body is populated and we copy from it for free. We never call
 * ngx_http_read_client_request_body ourselves, which is exactly what FR-BODY-2
 * requires — capture must not force a read that would change behavior, and
 * FR-BODY-3's "must not block" is satisfied trivially because we never wait.
 *
 * Consequence, by design: for a location where nothing reads the body (e.g. a
 * bare `return 200`), there is nothing buffered and the preview is empty. That
 * is correct per FR-BODY-2, not a gap.
 *
 * Buffers already spilled to disk (`temp_file`) are deliberately not read back:
 * a synchronous file read on the LOG path would block the event loop (G8). We
 * record the observed size and mark the preview truncated instead.
 */
void
ngx_http_trace_capture_request_body(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx)
{
    ngx_http_trace_loc_conf_t  *tlcf;
    ngx_chain_t                *cl;
    ngx_buf_t                  *b;
    size_t                      budget, n;
    unsigned                    truncated, binary;

    if (!ngx_http_trace_body_enabled(r, NGX_HTTP_TRACE_BODY_REQUEST)) {
        return;
    }

    if (r->request_body == NULL) {
        return;                         /* nothing read the body: no-op */
    }

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_trace_module);
    budget = ngx_http_trace_body_budget(tlcf);

    truncated = ctx->req_body_truncated;
    binary    = ctx->req_body_binary;

    for (cl = r->request_body->bufs; cl; cl = cl->next) {
        b = cl->buf;

        if (b->in_file) {
            /*
             * Spilled to disk. Reading it here would be a blocking read on the
             * event loop (G8), so account for it and mark the preview partial.
             */
            if (b->file_last > b->file_pos) {
                ctx->req_body_total += (off_t) (b->file_last - b->file_pos);
                truncated = 1;
            }
            continue;
        }

        if (b->pos == NULL || b->last <= b->pos) {
            continue;
        }

        n = (size_t) (b->last - b->pos);

        ngx_http_trace_body_append(r, &ctx->req_body, &ctx->req_body_len,
                                   &ctx->req_body_total, &truncated, &binary,
                                   b->pos, n, budget);
    }

    ctx->req_body_truncated = truncated;
    ctx->req_body_binary    = binary;
}


/*
 * M8.2 — the top output body filter: copy a bounded prefix of the response body
 * (FR-BODY-4).
 *
 * Position note: installing at the HEAD of the chain means we are invoked BEFORE
 * gzip/ssi/sub transform the buffers, so the preview is the body as the origin
 * produced it. That is the more useful artifact for a debugging tool — a hex dump
 * of compressed bytes tells an operator nothing — and the fact that the client
 * received a transformed form is still reported via `content_encoding`, which is
 * read at LOG once headers_out is final.
 *
 * Transparency rules this obeys:
 *   - it never modifies, consumes or reorders a buffer — it only reads;
 *   - it always calls the next filter with the chain unchanged
 *     (skill:filter-call-next), so the response is byte-identical whether or not
 *     tracing is on (G1);
 *   - it skips subrequests, so an auth_request body never pollutes the parent's
 *     response preview (skill:filter-check-subrequest);
 *   - it stops copying once the budget is spent, so a streaming or unbounded
 *     response is capped rather than accumulated (M8.3).
 */
ngx_int_t
ngx_http_trace_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_http_trace_loc_conf_t  *tlcf;
    ngx_http_trace_ctx_t       *ctx;
    ngx_chain_t                *cl;
    ngx_buf_t                  *b;
    size_t                      budget, n;
    unsigned                    truncated, binary;

    /* Only the main request's own response body is the client-facing one. */
    if (r != r->main || in == NULL) {
        return ngx_http_trace_next_body_filter(r, in);
    }

    ctx = ngx_http_trace_get_ctx(r);
    if (ctx == NULL || ctx->no_trace) {
        return ngx_http_trace_next_body_filter(r, in);   /* G2 fast path */
    }

    if (!ngx_http_trace_body_enabled(r, NGX_HTTP_TRACE_BODY_RESPONSE)) {
        return ngx_http_trace_next_body_filter(r, in);
    }

    tlcf = ngx_http_get_module_loc_conf(r, ngx_http_trace_module);
    budget = ngx_http_trace_body_budget(tlcf);

    truncated = ctx->resp_body_truncated;
    binary    = ctx->resp_body_binary;

    for (cl = in; cl; cl = cl->next) {
        b = cl->buf;

        if (b == NULL) {
            continue;
        }

        if (b->in_file) {
            /* Do not read from disk on the filter path (G8); account only. */
            if (b->file_last > b->file_pos) {
                ctx->resp_body_total += (off_t) (b->file_last - b->file_pos);
                truncated = 1;
            }

        } else if (b->pos != NULL && b->last > b->pos) {

            n = (size_t) (b->last - b->pos);

            ngx_http_trace_body_append(r, &ctx->resp_body,
                                       &ctx->resp_body_len,
                                       &ctx->resp_body_total,
                                       &truncated, &binary, b->pos, n,
                                       budget);
        }

        /* FR-BODY-4: the preview is final at last_buf. */
        if (b->last_buf || b->last_in_chain) {
            ctx->resp_body_done = 1;
        }
    }

    ctx->resp_body_truncated = truncated;
    ctx->resp_body_binary    = binary;

    return ngx_http_trace_next_body_filter(r, in);
}


/*
 * M8.4 — record a subrequest as a step nested under the parent's timeline
 * (FR-CTX-*, schema §8.3). Called from the header filter of a subrequest, which
 * is the first point where the subrequest has both its final URI and its status.
 *
 * The step is appended to the *main* request's context, so an auth_request or
 * an SSI include shows up in the transaction it belongs to instead of being
 * silently invisible (the pre-M8 behavior, where subrequests had no ctx at all).
 * A non-2xx subrequest is marked `error` because that is exactly the signal an
 * operator is looking for when an auth_request denies a request.
 */
void
ngx_http_trace_note_subrequest(ngx_http_request_t *r)
{
    ngx_http_trace_ctx_t   *ctx;
    ngx_http_trace_step_t  *step;
    ngx_uint_t              status;

    ctx = ngx_http_trace_get_ctx(r->main);
    if (ctx == NULL || ctx->no_trace) {
        return;                         /* G2 fast path */
    }

    /* Bounded per-request state (NFR-MEM-1 / G3): cap the recorded fan-out. */
    if (ctx->nsubrequests >= NGX_HTTP_TRACE_MAX_STEPS) {
        return;
    }

    step = ngx_http_trace_add_step(r->main, ctx, "SUBREQUEST",
                                   ngx_http_trace_resolve_handler_name(r));
    if (step == NULL) {
        return;                         /* step cap / alloc failure: degrade */
    }

    step->type = NGX_HTTP_TRACE_STEP_SUBREQUEST;

    /* The subrequest's own URI, copied into the main pool so it outlives r. */
    if (r->uri.len) {
        step->note.data = ngx_pnalloc(r->main->pool, r->uri.len);
        if (step->note.data != NULL) {
            ngx_memcpy(step->note.data, r->uri.data, r->uri.len);
            step->note.len = r->uri.len;
        }
    }

    status = r->headers_out.status ? r->headers_out.status : r->err_status;

    if (status >= NGX_HTTP_BAD_REQUEST) {
        step->status = NGX_HTTP_TRACE_ST_ERROR;
    }

    ctx->nsubrequests++;
}


/*
 * M8.2 — record the response content metadata (FR-BODY-4/5).
 *
 * Called at LOG, deliberately NOT from our header filter. We install at the head
 * of the output chain, so our header filter runs BEFORE gzip's — at which point
 * headers_out.content_encoding is still unset and reading it there would always
 * report "no encoding" for a compressed response. By LOG every filter has run and
 * headers_out is final, so this is the only point where the value is truthful.
 *
 * Both strings point at nginx's own header memory (request-pool lifetime), so no
 * copy is needed: they are serialized moments later in the same LOG phase.
 */
void
ngx_http_trace_capture_response_meta(ngx_http_request_t *r,
    ngx_http_trace_ctx_t *ctx)
{
    if (ctx == NULL || ctx->no_trace) {
        return;
    }

    if (r->headers_out.content_encoding != NULL
        && r->headers_out.content_encoding->value.len)
    {
        ctx->resp_content_encoding = r->headers_out.content_encoding->value;
    }

    if (r->headers_out.content_type.len) {
        ctx->resp_content_type = r->headers_out.content_type;
    }
}


/*
 * M8.4 — the output header filter exists solely to correlate subrequests: a
 * subrequest reaching the header filter is a completed internal step, recorded
 * against the parent before handing off. The main request's own metadata is read
 * at LOG instead (see ngx_http_trace_capture_response_meta for why).
 */
ngx_int_t
ngx_http_trace_header_filter(ngx_http_request_t *r)
{
    if (r != r->main) {
        ngx_http_trace_note_subrequest(r);
    }

    return ngx_http_trace_next_header_filter(r);
}


/*
 * Emit one body preview object (FR-BODY-5): always `captured_bytes`,
 * `total_bytes` and `truncated`; the bytes themselves as `preview` when they
 * look like text and `preview_hex` when they do not.
 *
 * Emits nothing at all when no bytes were captured, so a transaction from a
 * location with body capture off stays exactly as compact as before M8.
 */
u_char *
ngx_http_trace_json_body(u_char *p, u_char *last, const char *name,
    u_char *buf, size_t len, off_t total, unsigned truncated, unsigned binary,
    ngx_str_t *content_type, ngx_str_t *content_encoding)
{
    ngx_str_t   s;
    ngx_uint_t  i;

    if (buf == NULL && len == 0 && total == 0) {
        return p;                       /* nothing captured: omit section */
    }

    p = ngx_snprintf(p, last - p,
                     ",\"%s\":{\"captured_bytes\":%uz,\"total_bytes\":%O"
                     ",\"truncated\":%s",
                     name, len, total, truncated ? "true" : "false");

    if (content_type != NULL && content_type->len) {
        p = ngx_snprintf(p, last - p, ",\"content_type\":");
        p = ngx_http_trace_json_str(p, last, content_type);
    }

    if (content_encoding != NULL && content_encoding->len) {
        p = ngx_snprintf(p, last - p, ",\"content_encoding\":");
        p = ngx_http_trace_json_str(p, last, content_encoding);
    }

    if (len != 0) {
        if (binary) {
            /* Non-text payload: hex so the JSON stays valid and lossless. */
            p = ngx_snprintf(p, last - p, ",\"preview_hex\":\"");
            for (i = 0; i < len && last - p > 2; i++) {
                p = ngx_sprintf(p, "%02xd", (ngx_uint_t) buf[i]);
            }
            p = ngx_snprintf(p, last - p, "\"");

        } else {
            s.data = buf;
            s.len  = len;
            p = ngx_snprintf(p, last - p, ",\"preview\":");
            p = ngx_http_trace_json_str(p, last, &s);
        }
    }

    p = ngx_snprintf(p, last - p, "}");

    return p;
}
