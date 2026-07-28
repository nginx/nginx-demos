/*
 * ngx_http_trace_module - shared memory: zone, ring buffer & session store.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_trace_module.h"

/*
 * `trace_zone zone=NAME:SIZE;` — declare the slab-backed shared-memory zone.
 * Parses the zone= parameter, registers the zone via ngx_shared_memory_add,
 * and wires its init callback. The zone data pointer is our per-zone shctx.
 */
char *
ngx_http_trace_zone(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_trace_main_conf_t  *mcf = conf;

    ssize_t      size;
    ngx_str_t   *value, name;
    ngx_shm_zone_t  *shm_zone;

    value = cf->args->elts;

    /* Canonical syntax (SPEC FR-CFG-1): trace_zone <name> <size> */
    name = value[1];

    size = ngx_parse_size(&value[2]);
    if (size == NGX_ERROR) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid zone size \"%V\"", &value[2]);
        return NGX_CONF_ERROR;
    }

    if (size < (ssize_t) (8 * ngx_pagesize)) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "trace_zone \"%V\" is too small", &name);
        return NGX_CONF_ERROR;
    }

    shm_zone = ngx_shared_memory_add(cf, &name, size, &ngx_http_trace_module);
    if (shm_zone == NULL) {
        return NGX_CONF_ERROR;
    }

    if (shm_zone->data) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "duplicate trace_zone \"%V\"", &name);
        return NGX_CONF_ERROR;
    }

    shm_zone->init = ngx_http_trace_init_zone;
    shm_zone->data = mcf;              /* non-NULL marks it configured */

    mcf->shm_zone = shm_zone;

    return NGX_CONF_OK;
}

/*
 * Zone init callback. Runs at startup and on reload. On a fresh zone it
 * allocates the shctx, the bounded ring buffer, and the session store from slab
 * memory (M5.1/M5.2). On reload (data != NULL) it inherits the existing shared
 * segment untouched so in-flight sessions and captured transactions survive a
 * config reload (FR-SHM-3, skill:mem-shared-slab).
 */
ngx_int_t
ngx_http_trace_init_zone(ngx_shm_zone_t *shm_zone, void *data)
{
    ngx_http_trace_shctx_t  *octx = data;   /* previous incarnation, if reload */

    ngx_slab_pool_t         *shpool;
    ngx_http_trace_shctx_t  *shctx;
    ngx_uint_t               i;

    shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;

    if (octx) {
        /* reload: reuse the existing shared structures as-is (FR-SHM-3) */
        shm_zone->data = octx;
        return NGX_OK;
    }

    if (shm_zone->shm.exists) {
        /* zone already mapped by another process; adopt its data pointer */
        shm_zone->data = shpool->data;
        return NGX_OK;
    }

    shctx = ngx_slab_alloc(shpool, sizeof(ngx_http_trace_shctx_t));
    if (shctx == NULL) {
        return NGX_ERROR;
    }

    shctx->shpool = shpool;

    /* M5.2 ring buffer: a fixed array of slots, zero-initialized (empty). */
    shctx->ring = ngx_slab_calloc(shpool,
                      NGX_HTTP_TRACE_RING_SLOTS * sizeof(ngx_http_trace_slot_t));
    if (shctx->ring == NULL) {
        return NGX_ERROR;
    }
    shctx->head  = 0;
    shctx->count = 0;
    shctx->seq   = 0;

    /* M5.1 session store: a fixed array, all entries free (id == 0). */
    shctx->sessions = ngx_slab_calloc(shpool,
                      NGX_HTTP_TRACE_MAX_SESSIONS
                          * sizeof(ngx_http_trace_session_t));
    if (shctx->sessions == NULL) {
        return NGX_ERROR;
    }
    for (i = 0; i < NGX_HTTP_TRACE_MAX_SESSIONS; i++) {
        shctx->sessions[i].id = 0;      /* free */
    }
    shctx->nsessions   = 0;
    shctx->session_seq = 0;
    shctx->dropped     = 0;

    shpool->data = shctx;
    shm_zone->data = shctx;

    return NGX_OK;
}

/*
 * M5.4 — retention/TTL eviction. Walks the session store and marks any
 * capturing session past its `expires_at` as expired (stopped_reason=expired).
 * Fully expired sessions (past expiry) are freed so their id becomes unknown
 * again — AC-15 requires their endpoints to then 404. Called under the slab
 * mutex on every commit and every control read, so eviction is lazy and needs
 * no event-loop timer (portable; G8-friendly).
 *
 * Two-stage lifecycle per SPEC §8.3: at `expires_at` a capturing session first
 * transitions to state=expired (still viewable, stopped_reason=expired); once it
 * has additionally aged past its expiry it is removed from the store entirely.
 */
void
ngx_http_trace_expire_locked(ngx_http_trace_shctx_t *shctx, time_t now)
{
    ngx_http_trace_session_t  *s;
    ngx_uint_t                 i;

    for (i = 0; i < NGX_HTTP_TRACE_MAX_SESSIONS; i++) {
        s = &shctx->sessions[i];

        if (s->id == 0) {
            continue;                       /* free entry */
        }

        if (s->expires_at != 0 && now >= s->expires_at) {
            if (s->state == NGX_HTTP_TRACE_SESS_CAPTURING) {
                /* first crossing: stop capturing, remain viewable (grace) */
                s->state = NGX_HTTP_TRACE_SESS_EXPIRED;
                s->stopped_reason = NGX_HTTP_TRACE_STOP_EXPIRED;
            } else if (s->evict_at != 0 && now >= s->evict_at) {
                /* aged past the grace horizon: evict entirely (AC-15) */
                s->id = 0;                  /* free the slot */
                if (shctx->nsessions > 0) {
                    shctx->nsessions--;
                }
            }
        }
    }
}

/* Find a live session by id (caller holds the mutex). NULL if unknown/freed. */
ngx_http_trace_session_t *
ngx_http_trace_session_find_locked(ngx_http_trace_shctx_t *shctx, ngx_uint_t id)
{
    ngx_uint_t  i;

    if (id == 0) {
        return NULL;
    }

    for (i = 0; i < NGX_HTTP_TRACE_MAX_SESSIONS; i++) {
        if (shctx->sessions[i].id == id) {
            return &shctx->sessions[i];
        }
    }

    return NULL;
}

/*
 * Find the newest capturing session whose path-prefix filter matches uri
 * (caller holds the mutex). Returns the session id, or 0 if none matches.
 * "Newest" = highest id, so a later-created session wins overlap (M6.2).
 */
ngx_uint_t
ngx_http_trace_session_match_locked(ngx_http_trace_shctx_t *shctx,
    u_char *uri, size_t uri_len)
{
    ngx_http_trace_session_t  *s;
    ngx_uint_t                 i, best;

    best = 0;

    for (i = 0; i < NGX_HTTP_TRACE_MAX_SESSIONS; i++) {
        s = &shctx->sessions[i];

        if (s->id == 0 || s->state != NGX_HTTP_TRACE_SESS_CAPTURING) {
            continue;                       /* free or no longer capturing */
        }

        /* per-session transaction cap reached: stop matching new requests */
        if (s->max_transactions != 0 && s->captured >= s->max_transactions) {
            continue;
        }

        /* empty match => catch-all; otherwise uri must start with path_prefix */
        if (s->path_len != 0) {
            if (uri_len < s->path_len
                || ngx_memcmp(uri, s->path_prefix, s->path_len) != 0)
            {
                continue;
            }
        }

        if (s->id > best) {
            best = s->id;
        }
    }

    return best;
}
