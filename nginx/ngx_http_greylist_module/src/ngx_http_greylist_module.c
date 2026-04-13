/*
 * ngx_http_greylist_module.c  —  clean rewrite
 * See README.md for full documentation.
 * License: Apache 2.0
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#ifndef NGX_HTTP_TOO_MANY_REQUESTS
#define NGX_HTTP_TOO_MANY_REQUESTS 429
#endif

#define GL_FP_MAX      128
#define GL_KEY_MAX     160
#define GL_MAX_RULES    64
#define GL_CAP_SLOTS     3

#define GL_TYPE_GL  0
#define GL_TYPE_RL  1

typedef struct {
    ngx_str_t     pattern_str;
#if (NGX_PCRE)
    ngx_regex_t  *regex;
    ngx_uint_t    captures;
    ngx_flag_t    is_regex;
#endif
    ngx_uint_t    rate;
    ngx_uint_t    burst;
    ngx_uint_t    duration;
    ngx_uint_t    idx;
} gl_rule_t;

typedef struct {
    u_char      color;
    uint8_t     type;
    uint8_t     rule_idx;
    uint8_t     key_len;
    ngx_queue_t queue;
    time_t      expiry;
    ngx_msec_t  last_ms;
    ngx_uint_t  excess;
    u_char      key[1];
} gl_node_t;

typedef struct {
    ngx_rbtree_t       rbtree;
    ngx_rbtree_node_t  sentinel;
    ngx_queue_t        queue;
    ngx_slab_pool_t   *shpool;
} gl_shm_t;

typedef struct {
    ngx_str_t       name;
    gl_shm_t       *sh;
    ngx_slab_pool_t *shpool;
    ngx_shm_zone_t  *shm_zone;
    ngx_array_t     rules;
    ngx_uint_t      timeout;
} gl_zone_t;

typedef struct {
    ngx_array_t  zones;
} gl_main_cf_t;

typedef struct {
    ngx_str_t  zone_name;
} gl_loc_cf_t;

/* ── forward declarations ────────────────────────────────────────────────── */

static void      *gl_create_main_cf(ngx_conf_t *cf);
static void      *gl_create_loc_cf(ngx_conf_t *cf);
static char      *gl_merge_loc_cf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t  gl_init(ngx_conf_t *cf);
static char      *gl_zone_directive(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char      *gl_rule_directive(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char      *gl_directive(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t  gl_shm_init(ngx_shm_zone_t *shm_zone, void *data);
static ngx_int_t  gl_handler(ngx_http_request_t *r);
static gl_zone_t *gl_find_zone(gl_main_cf_t *mcf, ngx_str_t *name);
static void       gl_rbtree_insert(ngx_rbtree_node_t *temp,
                      ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel);
static gl_node_t *gl_lookup(gl_shm_t *sh, ngx_uint_t hash,
                      const u_char *key, uint8_t klen);
static void       gl_expire(gl_zone_t *gz, ngx_uint_t n);
static uint32_t   gl_fnv1a32(const u_char *data, size_t len);
static size_t     gl_fingerprint(ngx_http_request_t *r, u_char *buf, size_t max);
static size_t     gl_match_subject(ngx_http_request_t *r, u_char *buf, size_t max);
static ngx_int_t  gl_match_rule(ngx_http_request_t *r, gl_rule_t *rule,
                      u_char *sbuf, size_t slen);
static ngx_int_t  gl_parse_rate(ngx_str_t *s, ngx_uint_t *out);
static ngx_int_t  gl_parse_duration(ngx_str_t *s, ngx_uint_t *out);

/* ── commands ────────────────────────────────────────────────────────────── */

static ngx_command_t gl_commands[] = {
    { ngx_string("greylist_zone"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE23,
      gl_zone_directive,
      NGX_HTTP_MAIN_CONF_OFFSET, 0, NULL },

    { ngx_string("greylist_rule"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_1MORE,
      gl_rule_directive,
      NGX_HTTP_MAIN_CONF_OFFSET, 0, NULL },

    { ngx_string("greylist"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF
      | NGX_CONF_TAKE1,
      gl_directive,
      NGX_HTTP_LOC_CONF_OFFSET, 0, NULL },

    ngx_null_command
};

static ngx_http_module_t gl_module_ctx = {
    NULL, gl_init,
    gl_create_main_cf, NULL,
    NULL, NULL,
    gl_create_loc_cf, gl_merge_loc_cf
};

ngx_module_t ngx_http_greylist_module = {
    NGX_MODULE_V1,
    &gl_module_ctx, gl_commands, NGX_HTTP_MODULE,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NGX_MODULE_V1_PADDING
};

/* ── FNV-1a 32-bit ───────────────────────────────────────────────────────── */

static uint32_t
gl_fnv1a32(const u_char *data, size_t len)
{
    uint32_t h = 0x811c9dc5u;
    while (len--) { h ^= *data++; h *= 0x01000193u; }
    return h;
}

/* ── fingerprint: IP|fnv32(UA)|fnv32(token) ──────────────────────────────── */

static size_t
gl_fingerprint(ngx_http_request_t *r, u_char *buf, size_t max)
{
    u_char    *p;
    uint32_t   ua_h, tok_h;
    ngx_str_t  ua, auth, token;

    ua = r->headers_in.user_agent
         ? r->headers_in.user_agent->value
         : (ngx_str_t) ngx_null_string;

    ngx_str_null(&token);
    if (r->headers_in.authorization) {
        auth = r->headers_in.authorization->value;
        if (auth.len > 7
            && ngx_strncasecmp(auth.data, (u_char *)"Bearer ", 7) == 0) {
            token.data = auth.data + 7;
            token.len  = auth.len  - 7;
            while (token.len && token.data[0] == ' ')
                { token.data++; token.len--; }
        }
    }

    ua_h  = gl_fnv1a32(ua.data,    ua.len);
    tok_h = gl_fnv1a32(token.data, token.len);
    p = ngx_snprintf(buf, max, "%V|%08xD|%08xD",
                     &r->connection->addr_text, ua_h, tok_h);
    return (size_t)(p - buf);
}

/* ── match subject: METHOD:scheme://host/uri ──────────────────────────────── */

static size_t
gl_match_subject(ngx_http_request_t *r, u_char *buf, size_t max)
{
    ngx_str_t  scheme;
    u_char    *p;

#if (NGX_HTTP_SSL)
    if (r->connection->ssl) { ngx_str_set(&scheme, "https"); } else { ngx_str_set(&scheme, "http"); }
#else
    ngx_str_set(&scheme, "http");
#endif

    p = ngx_snprintf(buf, max - 1, "%V:%V://%V%V%s%V",
                     &r->method_name, &scheme,
                     &r->headers_in.server, &r->uri,
                     r->args.len ? "?" : "", &r->args);
    return (size_t)(p - buf);
}

/* ── pattern matching ────────────────────────────────────────────────────── */

static ngx_int_t
gl_match_rule(ngx_http_request_t *r, gl_rule_t *rule,
    u_char *sbuf, size_t slen)
{
#if (NGX_PCRE)
    if (rule->is_regex && rule->regex != NULL) {
        ngx_str_t  subject;
        ngx_int_t  rc;
        int        captures[GL_CAP_SLOTS];

        subject.data = sbuf;
        subject.len  = slen;

        /*
         * ngx_regex_exec uses the same PCRE/PCRE2 instance nginx was
         * compiled against — no cross-library ABI issues.
         * Returns >= 0 on match, NGX_REGEX_NO_MATCHED (-1) on no match.
         */
        rc = ngx_regex_exec(rule->regex, &subject, captures, GL_CAP_SLOTS);

        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
            "greylist: regex \"%V\" vs \"%*s\" rc=%i (%s)",
            &rule->pattern_str, (int) slen, sbuf, rc,
            rc >= 0 ? "MATCH" : "no match");

        return (rc >= 0) ? NGX_OK : NGX_DECLINED;
    }
#endif

    return (rule->pattern_str.len == slen
            && ngx_memcmp(rule->pattern_str.data, sbuf, slen) == 0)
           ? NGX_OK : NGX_DECLINED;
}

/* ── rbtree ──────────────────────────────────────────────────────────────── */

static void
gl_rbtree_insert(ngx_rbtree_node_t *temp,
    ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel)
{
    ngx_rbtree_node_t **p;
    gl_node_t          *n, *t;

    for (;;) {
        n = (gl_node_t *) &node->color;
        t = (gl_node_t *) &temp->color;

        if      (node->key < temp->key) { p = &temp->left;  }
        else if (node->key > temp->key) { p = &temp->right; }
        else {
            p = (ngx_memn2cmp(n->key, t->key, n->key_len, t->key_len) < 0)
                ? &temp->left : &temp->right;
        }
        if (*p == sentinel) break;
        temp = *p;
    }

    *p = node;
    node->parent = temp;
    node->left   = sentinel;
    node->right  = sentinel;
    ngx_rbt_red(node);
}

static gl_node_t *
gl_lookup(gl_shm_t *sh, ngx_uint_t hash, const u_char *key, uint8_t klen)
{
    ngx_rbtree_node_t *node = sh->rbtree.root, *sent = &sh->sentinel;
    ngx_int_t rc;
    gl_node_t *n;

    while (node != sent) {
        if      (hash < node->key) { node = node->left;  continue; }
        else if (hash > node->key) { node = node->right; continue; }
        n  = (gl_node_t *) &node->color;
        rc = ngx_memn2cmp((u_char *) key, n->key, klen, n->key_len);
        if (rc == 0) return n;
        node = rc < 0 ? node->left : node->right;
    }
    return NULL;
}

static void
gl_expire(gl_zone_t *gz, ngx_uint_t n)
{
    gl_shm_t          *sh  = gz->sh;
    time_t             now = ngx_time();
    ngx_queue_t       *q;
    gl_node_t         *nd;
    ngx_rbtree_node_t *rbn;

    while (n--) {
        if (ngx_queue_empty(&sh->queue)) return;
        q  = ngx_queue_last(&sh->queue);
        nd = ngx_queue_data(q, gl_node_t, queue);

        if (nd->type == GL_TYPE_GL) {
            if (now < nd->expiry + (time_t) gz->timeout) return;
        } else {
            if (now < (time_t)(nd->last_ms / 1000) + (time_t) gz->timeout)
                return;
        }

        ngx_queue_remove(q);
        rbn = (ngx_rbtree_node_t *)
              ((u_char *) nd - offsetof(ngx_rbtree_node_t, color));
        ngx_rbtree_delete(&sh->rbtree, rbn);
        ngx_slab_free_locked(sh->shpool, rbn);
    }
}

/* ── helper: push a response header ──────────────────────────────────────── */

static void
gl_set_header(ngx_http_request_t *r, const char *key, ngx_str_t *val)
{
    ngx_table_elt_t *h = ngx_list_push(&r->headers_out.headers);
    if (!h) return;
    h->hash        = 1;
    h->key.data    = (u_char *) key;
    h->key.len     = ngx_strlen(key);
    h->value       = *val;
    h->lowcase_key = h->key.data;   /* already lowercase */
}

/* ── access-phase handler ────────────────────────────────────────────────── */

static ngx_int_t
gl_handler(ngx_http_request_t *r)
{
    gl_loc_cf_t        *lcf;
    gl_main_cf_t       *mcf;
    gl_zone_t          *gz;
    gl_shm_t           *sh;
    gl_rule_t          *rules;
    gl_node_t          *node;
    ngx_rbtree_node_t  *rbn;

    u_char      fp[GL_FP_MAX], gl_key[GL_KEY_MAX], rl_key[GL_KEY_MAX];
    u_char      sbuf[2048];
    size_t      fp_len, gl_klen, rl_klen, slen;
    ngx_uint_t  gl_hash, rl_hash, i, matched;
    ngx_msec_t  now_ms;
    time_t      now;
    ngx_int_t   excess;
    size_t      node_size;
    ngx_uint_t  retry_after;
    u_char      ra_buf[NGX_INT_T_LEN];
    ngx_str_t   ra_str, one;

    lcf = ngx_http_get_module_loc_conf(r, ngx_http_greylist_module);
    if (lcf->zone_name.len == 0) return NGX_DECLINED;

    mcf = ngx_http_get_module_main_conf(r, ngx_http_greylist_module);
    gz  = gl_find_zone(mcf, &lcf->zone_name);
    if (!gz) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
            "greylist: zone \"%V\" not found", &lcf->zone_name);
        return NGX_DECLINED;
    }

    sh    = gz->sh;
    rules = gz->rules.elts;

    fp_len = gl_fingerprint(r, fp, sizeof(fp));
    slen   = gl_match_subject(r, sbuf, sizeof(sbuf));

    gl_key[0] = 'G'; gl_key[1] = 'L'; gl_key[2] = ':';
    ngx_memcpy(gl_key + 3, fp, fp_len);
    gl_klen = 3 + fp_len;
    gl_hash = gl_fnv1a32(gl_key, gl_klen);

    /* find matching rule — must be outside the shmtx (PCRE not reentrant) */
    matched = gz->rules.nelts;
    for (i = 0; i < gz->rules.nelts; i++) {
        if (gl_match_rule(r, &rules[i], sbuf, slen) == NGX_OK) {
            matched = i; break;
        }
    }

    ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
        "greylist: zone=\"%V\" rules=%uD subject=\"%*s\" matched=%s "
        "fp=\"%*s\"",
        &gz->name, (ngx_uint_t) gz->rules.nelts,
        (int) slen, sbuf,
        matched < gz->rules.nelts ? "YES" : "NONE",
        (int) fp_len, fp);

    now    = ngx_time();
    now_ms = ngx_current_msec;

    ngx_shmtx_lock(&sh->shpool->mutex);

    /* ── phase 1: greylist check ─────────────────────────────────────── */

    node = gl_lookup(sh, gl_hash, gl_key, (uint8_t) gl_klen);
    if (node != NULL) {
        if (now < node->expiry) {
            retry_after = (ngx_uint_t)(node->expiry - now);
            ngx_shmtx_unlock(&sh->shpool->mutex);
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                "greylist: BLOCKED fp=\"%*s\" retry_after=%uDs",
                (int) fp_len, fp, retry_after);
            goto send_429;
        }
        /* logically expired — evict */
        ngx_queue_remove(&node->queue);
        rbn = (ngx_rbtree_node_t *)
              ((u_char *) node - offsetof(ngx_rbtree_node_t, color));
        ngx_rbtree_delete(&sh->rbtree, rbn);
        ngx_slab_free_locked(sh->shpool, rbn);
        node = NULL;
    }

    /* ── phase 2: rate-limit ─────────────────────────────────────────── */

    if (matched == gz->rules.nelts) {
        ngx_shmtx_unlock(&sh->shpool->mutex);
        return NGX_DECLINED;
    }

    i = matched;

    rl_klen = (size_t)(ngx_snprintf(rl_key, sizeof(rl_key),
                       "RL:%02uD:%*s", i, (int) fp_len, fp) - rl_key);
    rl_hash = gl_fnv1a32(rl_key, rl_klen);

    node = gl_lookup(sh, rl_hash, rl_key, (uint8_t) rl_klen);

    if (node == NULL) {
        gl_expire(gz, 1);

        node_size = offsetof(ngx_rbtree_node_t, color)
                  + offsetof(gl_node_t, key)
                  + rl_klen;

        rbn = ngx_slab_alloc_locked(sh->shpool, node_size);
        if (!rbn) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                "greylist: zone \"%V\" OOM, allowing request", &gz->name);
            ngx_shmtx_unlock(&sh->shpool->mutex);
            return NGX_DECLINED;
        }

        node           = (gl_node_t *) &rbn->color;
        rbn->key       = rl_hash;
        node->type     = GL_TYPE_RL;
        node->rule_idx = (uint8_t) i;
        node->key_len  = (uint8_t) rl_klen;
        node->excess   = 0;
        node->last_ms  = now_ms;
        ngx_memcpy(node->key, rl_key, rl_klen);
        ngx_rbtree_insert(&sh->rbtree, rbn);
        ngx_queue_insert_head(&sh->queue, &node->queue);

        /* first request: allow, start tracking from next */
        ngx_shmtx_unlock(&sh->shpool->mutex);
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
            "greylist: RL new node rule=%uD fp=\"%*s\" first-request allowed",
            i, (int) fp_len, fp);
        return NGX_DECLINED;
    }

    /* leaky-bucket */
    excess = (ngx_int_t) node->excess
           - (ngx_int_t)(rules[i].rate
                         * (ngx_msec_int_t)(now_ms - node->last_ms)
                         / 1000)
           + 1000;

    if (excess < 0) excess = 0;
    node->last_ms = now_ms;

    ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
        "greylist: RL rule=%uD excess=%i burst=%uD — %s",
        i, excess, rules[i].burst,
        (ngx_uint_t) excess > rules[i].burst ? "BLOCK" : "pass");

    if ((ngx_uint_t) excess > rules[i].burst) {
        node->excess = (ngx_uint_t) excess;
        ngx_queue_remove(&node->queue);
        ngx_queue_insert_head(&sh->queue, &node->queue);

        gl_node_t *gn = gl_lookup(sh, gl_hash, gl_key, (uint8_t) gl_klen);
        if (!gn) {
            gl_expire(gz, 1);
            node_size = offsetof(ngx_rbtree_node_t, color)
                      + offsetof(gl_node_t, key) + gl_klen;
            ngx_rbtree_node_t *grbn =
                ngx_slab_alloc_locked(sh->shpool, node_size);
            if (grbn) {
                gn           = (gl_node_t *) &grbn->color;
                grbn->key    = gl_hash;
                gn->type     = GL_TYPE_GL;
                gn->rule_idx = (uint8_t) i;
                gn->key_len  = (uint8_t) gl_klen;
                gn->expiry   = now + (time_t) rules[i].duration;
                ngx_memcpy(gn->key, gl_key, gl_klen);
                ngx_rbtree_insert(&sh->rbtree, grbn);
                ngx_queue_insert_head(&sh->queue, &gn->queue);
            }
        } else {
            gn->expiry = now + (time_t) rules[i].duration;
            ngx_queue_remove(&gn->queue);
            ngx_queue_insert_head(&sh->queue, &gn->queue);
        }

        retry_after = rules[i].duration;
        ngx_shmtx_unlock(&sh->shpool->mutex);
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
            "greylist: GREYLISTED fp=\"%*s\" rule=%uD duration=%uDs",
            (int) fp_len, fp, i, rules[i].duration);
        goto send_429;
    }

    node->excess = (ngx_uint_t) excess;
    ngx_queue_remove(&node->queue);
    ngx_queue_insert_head(&sh->queue, &node->queue);
    ngx_shmtx_unlock(&sh->shpool->mutex);
    return NGX_DECLINED;

send_429:
    ra_str.data = ra_buf;
    ra_str.len  = (size_t)(ngx_sprintf(ra_buf, "%uD", retry_after) - ra_buf);
    gl_set_header(r, "retry-after", &ra_str);

    ngx_str_set(&one, "1");
    gl_set_header(r, "x-greylisted", &one);

    return NGX_HTTP_TOO_MANY_REQUESTS;
}

/* ── parse helpers ───────────────────────────────────────────────────────── */

static ngx_int_t
gl_parse_rate(ngx_str_t *s, ngx_uint_t *out)
{
    u_char    *p = s->data, *end = s->data + s->len;
    size_t     dlen = 0;
    ngx_int_t  n;

    while (p + dlen < end && p[dlen] >= '0' && p[dlen] <= '9') dlen++;
    if (!dlen) return NGX_ERROR;
    n = ngx_atoi(p, dlen);
    if (n <= 0) return NGX_ERROR;
    p += dlen;

    if (p >= end || *p++ != 'r') return NGX_ERROR;
    if (p >= end || *p++ != '/') return NGX_ERROR;
    if (p >= end)                return NGX_ERROR;

    if      (*p == 's') *out = (ngx_uint_t) n * 1000;
    else if (*p == 'm') *out = (ngx_uint_t) n * 1000 / 60;
    else                return NGX_ERROR;

    if (*out == 0) *out = 1;
    return NGX_OK;
}

static ngx_int_t
gl_parse_duration(ngx_str_t *s, ngx_uint_t *out)
{
    size_t len = s->len;
    ngx_int_t n;
    if (len && s->data[len - 1] == 's') len--;
    n = ngx_atoi(s->data, len);
    if (n <= 0) return NGX_ERROR;
    *out = (ngx_uint_t) n;
    return NGX_OK;
}

static gl_zone_t *
gl_find_zone(gl_main_cf_t *mcf, ngx_str_t *name)
{
    ngx_uint_t i;
    gl_zone_t *z = mcf->zones.elts;
    for (i = 0; i < mcf->zones.nelts; i++)
        if (z[i].name.len == name->len
            && ngx_memcmp(z[i].name.data, name->data, name->len) == 0)
            return &z[i];
    return NULL;
}

/* ── directive handlers ──────────────────────────────────────────────────── */

static char *
gl_zone_directive(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    gl_main_cf_t   *mcf   = conf;
    ngx_str_t      *value = cf->args->elts;
    gl_zone_t      *zone;
    ngx_shm_zone_t *shm;
    ssize_t         size;
    ngx_uint_t      timeout = 3600, i;

    size = ngx_parse_size(&value[2]);
    if (size < (ssize_t)(8 * ngx_pagesize)) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "greylist_zone: invalid size \"%V\"", &value[2]);
        return NGX_CONF_ERROR;
    }

    for (i = 3; i < cf->args->nelts; i++) {
        if (ngx_strncmp(value[i].data, "timeout=", 8) == 0) {
            ngx_str_t s = { value[i].len - 8, value[i].data + 8 };
            if (gl_parse_duration(&s, &timeout) != NGX_OK) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                    "greylist_zone: invalid timeout \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }
        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "greylist_zone: unknown parameter \"%V\"", &value[i]);
            return NGX_CONF_ERROR;
        }
    }

    if (gl_find_zone(mcf, &value[1])) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "greylist_zone: \"%V\" already defined", &value[1]);
        return NGX_CONF_ERROR;
    }

    zone = ngx_array_push(&mcf->zones);
    if (!zone) return NGX_CONF_ERROR;
    ngx_memzero(zone, sizeof(*zone));
    zone->name    = value[1];
    zone->timeout = timeout;

    if (ngx_array_init(&zone->rules, cf->pool, 4, sizeof(gl_rule_t)) != NGX_OK)
        return NGX_CONF_ERROR;

    shm = ngx_shared_memory_add(cf, &value[1], (size_t) size,
                                &ngx_http_greylist_module);
    if (!shm) return NGX_CONF_ERROR;
    if (shm->data) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "greylist_zone: \"%V\" already defined", &value[1]);
        return NGX_CONF_ERROR;
    }

    shm->init      = gl_shm_init;
    shm->data      = zone;
    zone->shm_zone = shm;
    return NGX_CONF_OK;
}

static char *
gl_rule_directive(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    gl_main_cf_t  *mcf   = conf;
    ngx_str_t     *value = cf->args->elts;
    gl_zone_t     *zone;
    gl_rule_t     *rule;
    ngx_str_t      zone_name, pattern;
    ngx_uint_t     rate = 0, burst = 0, duration = 60, i;

    ngx_str_null(&zone_name);
    ngx_str_null(&pattern);

    for (i = 1; i < cf->args->nelts; i++) {
        if (ngx_strncmp(value[i].data, "zone=", 5) == 0) {
            zone_name.data = value[i].data + 5;
            zone_name.len  = value[i].len  - 5;

        } else if (ngx_strncmp(value[i].data, "pattern=", 8) == 0) {
            pattern.data = value[i].data + 8;
            pattern.len  = value[i].len  - 8;

            /*
             * nginx does NOT strip quotes from a token that does not
             * itself start with '"'.  The token "pattern=..." starts
             * with 'p', so quotes around the value remain verbatim.
             * Strip them here.
             */
            if (pattern.len >= 2
                && pattern.data[0] == '"'
                && pattern.data[pattern.len - 1] == '"')
            {
                pattern.data++;
                pattern.len -= 2;
            }

        } else if (ngx_strncmp(value[i].data, "rate=", 5) == 0) {
            ngx_str_t s = { value[i].len - 5, value[i].data + 5 };
            if (gl_parse_rate(&s, &rate) != NGX_OK) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                    "greylist_rule: invalid rate \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }

        } else if (ngx_strncmp(value[i].data, "burst=", 6) == 0) {
            ngx_str_t s  = { value[i].len - 6, value[i].data + 6 };
            ngx_int_t  n = ngx_atoi(s.data, s.len);
            if (n < 0) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                    "greylist_rule: invalid burst \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }
            burst = (ngx_uint_t) n * 1000;

        } else if (ngx_strncmp(value[i].data, "duration=", 9) == 0) {
            ngx_str_t s = { value[i].len - 9, value[i].data + 9 };
            if (gl_parse_duration(&s, &duration) != NGX_OK) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                    "greylist_rule: invalid duration \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }

        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "greylist_rule: unknown parameter \"%V\"", &value[i]);
            return NGX_CONF_ERROR;
        }
    }

    if (!zone_name.len || !pattern.len || !rate) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "greylist_rule: zone=, pattern= and rate= are required");
        return NGX_CONF_ERROR;
    }

    zone = gl_find_zone(mcf, &zone_name);
    if (!zone) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "greylist_rule: zone \"%V\" not found "
            "(define it first with greylist_zone)", &zone_name);
        return NGX_CONF_ERROR;
    }

    if (zone->rules.nelts >= GL_MAX_RULES) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "greylist_rule: too many rules (max %d)", GL_MAX_RULES);
        return NGX_CONF_ERROR;
    }

    rule = ngx_array_push(&zone->rules);
    if (!rule) return NGX_CONF_ERROR;
    ngx_memzero(rule, sizeof(*rule));

    rule->pattern_str = pattern;
    rule->rate        = rate;
    rule->burst       = burst;
    rule->duration    = duration;
    rule->idx         = zone->rules.nelts - 1;

#if (NGX_PCRE)
    if (pattern.len > 0 && pattern.data[0] == '~') {
        ngx_regex_compile_t  rc;
        u_char               errstr[NGX_MAX_CONF_ERRSTR];
        ngx_flag_t           caseless;
        ngx_str_t            re_str;

        caseless    = (pattern.len > 1 && pattern.data[1] == '*');
        re_str.data = pattern.data + (caseless ? 2 : 1);
        re_str.len  = pattern.len  - (caseless ? 2 : 1);

        ngx_memzero(&rc, sizeof(rc));
        rc.pattern  = re_str;
        rc.pool     = cf->pool;
        rc.err.len  = sizeof(errstr);
        rc.err.data = errstr;
        rc.options  = caseless ? NGX_REGEX_CASELESS : 0;

        if (ngx_regex_compile(&rc) != NGX_OK) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "greylist_rule: regex error: %V", &rc.err);
            return NGX_CONF_ERROR;
        }

        rule->regex    = rc.regex;
        rule->captures = (ngx_uint_t) rc.captures;
        rule->is_regex = 1;

        ngx_conf_log_error(NGX_LOG_NOTICE, cf, 0,
            "greylist_rule: compiled \"%V\" captures=%uD regex=%p",
            &pattern, rule->captures, rule->regex);
    }
#else
    if (pattern.len && pattern.data[0] == '~') {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "greylist_rule: regex requires nginx --with-pcre");
        return NGX_CONF_ERROR;
    }
#endif

    return NGX_CONF_OK;
}

static char *
gl_directive(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    gl_loc_cf_t  *lcf   = conf;
    ngx_str_t    *value = cf->args->elts;

    if (ngx_strncmp(value[1].data, "zone=", 5) != 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "greylist: expected zone=<n>, got \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    lcf->zone_name.data = value[1].data + 5;
    lcf->zone_name.len  = value[1].len  - 5;
    return NGX_CONF_OK;
}

/* ── module lifecycle ────────────────────────────────────────────────────── */

static ngx_int_t
gl_shm_init(ngx_shm_zone_t *shm_zone, void *data)
{
    gl_zone_t       *zone   = shm_zone->data;
    gl_zone_t       *ozone  = data;
    ngx_slab_pool_t *shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;
    gl_shm_t        *sh;

    if (ozone) {
        zone->sh     = ozone->sh;
        zone->shpool = ozone->shpool;
        return NGX_OK;
    }

    zone->shpool = shpool;
    sh = ngx_slab_alloc(shpool, sizeof(*sh));
    if (!sh) {
        ngx_log_error(NGX_LOG_EMERG, shm_zone->shm.log, 0,
            "greylist: zone \"%V\": insufficient shared memory", &zone->name);
        return NGX_ERROR;
    }

    zone->sh     = sh;
    shpool->data = sh;
    sh->shpool   = shpool;
    ngx_rbtree_init(&sh->rbtree, &sh->sentinel, gl_rbtree_insert);
    ngx_queue_init(&sh->queue);

    shpool->log_ctx = ngx_slab_alloc(shpool, zone->name.len + 32);
    if (shpool->log_ctx)
        ngx_sprintf(shpool->log_ctx, " in greylist zone \"%V\"%Z", &zone->name);

    return NGX_OK;
}

static void *
gl_create_main_cf(ngx_conf_t *cf)
{
    gl_main_cf_t *mcf = ngx_pcalloc(cf->pool, sizeof(*mcf));
    if (!mcf) return NULL;
    if (ngx_array_init(&mcf->zones, cf->pool, 4, sizeof(gl_zone_t)) != NGX_OK)
        return NULL;
    return mcf;
}

static void *
gl_create_loc_cf(ngx_conf_t *cf)
{
    return ngx_pcalloc(cf->pool, sizeof(gl_loc_cf_t));
}

static char *
gl_merge_loc_cf(ngx_conf_t *cf, void *parent, void *child)
{
    gl_loc_cf_t *prev = parent, *conf = child;
    ngx_conf_merge_str_value(conf->zone_name, prev->zone_name, "");
    return NGX_CONF_OK;
}

static ngx_int_t
gl_init(ngx_conf_t *cf)
{
    ngx_http_core_main_conf_t *cmcf =
        ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    ngx_http_handler_pt *h =
        ngx_array_push(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers);
    if (!h) return NGX_ERROR;
    *h = gl_handler;
    return NGX_OK;
}
