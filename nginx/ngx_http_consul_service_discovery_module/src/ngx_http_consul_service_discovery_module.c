/*
 * ngx_http_consul_service_discovery_module.c  (v2 – enhanced output)
 *
 * New vs v1:
 *   id     – consul_service_id "...";  auto: "<name>-<first-tag>" if unset
 *   tags   – JSON array  (split on comma from consul_service_tags)
 *   meta   – JSON object from repeated: consul_service_meta key value;
 *   checks – JSON array  from repeated: consul_service_check name=... tcp|http|grpc=... interval=... timeout=...;
 *
 * Copyright (c) 2024-2025 Fabrizio Fiorucci
 * Apache 2.0 License
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/* ═══════════════════════════════════════════════════════════════════════
 *  Data structures
 * ═══════════════════════════════════════════════════════════════════════ */

typedef struct {
    ngx_str_t  name;      /* check name (displayed in Consul UI) */
    ngx_str_t  type;      /* "tcp" | "http" | "grpc" | "ttl"    */
    ngx_str_t  target;    /* host:port  or  full URL             */
    ngx_str_t  interval;  /* e.g. "10s"                          */
    ngx_str_t  timeout;   /* e.g. "1s"                           */
} ngx_http_consul_check_t;


/* per-server{} config -------------------------------------------------- */
typedef struct {
    ngx_flag_t   discoverable;
    ngx_str_t    name;          /* consul_service_name            */
    ngx_str_t    id;            /* consul_service_id              */
    ngx_str_t    tags;          /* consul_service_tags (csv)      */
    ngx_array_t *meta;          /* ngx_keyval_t[]                 */
    ngx_array_t *checks;        /* ngx_http_consul_check_t[]      */
    ngx_int_t    port_override;
    ngx_str_t    address_override;
} ngx_http_consul_srv_conf_t;


/* per-location{} config ------------------------------------------------ */
typedef struct {
    ngx_flag_t  enabled;
} ngx_http_consul_loc_conf_t;


/* runtime catalogue entry (built at postconfiguration) ----------------- */
typedef struct {
    ngx_str_t    id;
    ngx_str_t    name;
    ngx_str_t    address;
    ngx_int_t    port;
    ngx_array_t *tags;          /* ngx_str_t[]                   */
    ngx_array_t *meta;          /* ngx_keyval_t[]                */
    ngx_array_t *checks;        /* ngx_http_consul_check_t[]     */
} ngx_http_consul_service_t;


/* http{} main config --------------------------------------------------- */
typedef struct {
    ngx_array_t *services;      /* ngx_http_consul_service_t[]   */
} ngx_http_consul_main_conf_t;


/* ═══════════════════════════════════════════════════════════════════════
 *  Forward declarations
 * ═══════════════════════════════════════════════════════════════════════ */

static ngx_int_t  ngx_http_consul_postconfiguration(ngx_conf_t *cf);
static void      *ngx_http_consul_create_main_conf(ngx_conf_t *cf);
static void      *ngx_http_consul_create_srv_conf(ngx_conf_t *cf);
static char      *ngx_http_consul_merge_srv_conf(ngx_conf_t *cf,
                      void *parent, void *child);
static void      *ngx_http_consul_create_loc_conf(ngx_conf_t *cf);
static char      *ngx_http_consul_merge_loc_conf(ngx_conf_t *cf,
                      void *parent, void *child);
static ngx_int_t  ngx_http_consul_handler(ngx_http_request_t *r);
static char      *ngx_http_consul_discovery_cmd(ngx_conf_t *cf,
                      ngx_command_t *cmd, void *conf);
static char      *ngx_http_consul_meta_cmd(ngx_conf_t *cf,
                      ngx_command_t *cmd, void *conf);
static char      *ngx_http_consul_check_cmd(ngx_conf_t *cf,
                      ngx_command_t *cmd, void *conf);

static ngx_int_t  ngx_http_consul_find_port(ngx_conf_t *cf,
                      ngx_http_core_srv_conf_t *target, ngx_uint_t *out);
static ngx_int_t  ngx_http_consul_find_addr(ngx_conf_t *cf,
                      ngx_http_core_srv_conf_t *target, ngx_str_t *out);
static ngx_array_t *ngx_http_consul_split_tags(ngx_pool_t *pool,
                      ngx_str_t *csv);

static size_t     ngx_http_consul_escape_len(ngx_str_t *s);
static u_char    *ngx_http_consul_escape(u_char *dst, ngx_str_t *s);
static size_t     ngx_http_consul_int_len(ngx_int_t n);
static size_t     ngx_http_consul_json_len(ngx_http_consul_main_conf_t *mcf);
static u_char    *ngx_http_consul_json_fill(u_char *p,
                      ngx_http_consul_main_conf_t *mcf);


/* ═══════════════════════════════════════════════════════════════════════
 *  Directives
 * ═══════════════════════════════════════════════════════════════════════ */

static ngx_command_t  ngx_http_consul_commands[] = {

    /* location{} ─────────────────────────────────────────────────────── */
    { ngx_string("consul_service_discovery"),
      NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_http_consul_discovery_cmd,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    /* server{} ────────────────────────────────────────────────────────── */
    { ngx_string("consul_service_discoverable"),
      NGX_HTTP_SRV_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_consul_srv_conf_t, discoverable),
      NULL },

    { ngx_string("consul_service_name"),
      NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_consul_srv_conf_t, name),
      NULL },

    /* NEW: explicit service ID */
    { ngx_string("consul_service_id"),
      NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_consul_srv_conf_t, id),
      NULL },

    { ngx_string("consul_service_tags"),
      NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_consul_srv_conf_t, tags),
      NULL },

    /* NEW: repeatable key-value metadata
     *   consul_service_meta env prod;
     *   consul_service_meta team payments;
     */
    { ngx_string("consul_service_meta"),
      NGX_HTTP_SRV_CONF|NGX_CONF_TAKE2,
      ngx_http_consul_meta_cmd,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      NULL },

    /* NEW: repeatable health check (key=value tokens)
     *   consul_service_check name=tcp  tcp=10.0.1.5:18880  interval=10s  timeout=1s;
     *   consul_service_check name=web  http=http://10.0.1.5/health  interval=15s  timeout=2s;
     * Recognised keys: name, tcp, http, grpc, ttl, interval, timeout
     */
    { ngx_string("consul_service_check"),
      NGX_HTTP_SRV_CONF|NGX_CONF_1MORE,
      ngx_http_consul_check_cmd,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("consul_service_port_override"),
      NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_consul_srv_conf_t, port_override),
      NULL },

    { ngx_string("consul_service_address_override"),
      NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_consul_srv_conf_t, address_override),
      NULL },

    ngx_null_command
};


/* ═══════════════════════════════════════════════════════════════════════
 *  Module context + module struct
 * ═══════════════════════════════════════════════════════════════════════ */

static ngx_http_module_t  ngx_http_consul_module_ctx = {
    NULL,                                  /* preconfiguration          */
    ngx_http_consul_postconfiguration,     /* postconfiguration         */
    ngx_http_consul_create_main_conf,      /* create main configuration */
    NULL,                                  /* init   main configuration */
    ngx_http_consul_create_srv_conf,       /* create server config      */
    ngx_http_consul_merge_srv_conf,        /* merge  server config      */
    ngx_http_consul_create_loc_conf,       /* create location config    */
    ngx_http_consul_merge_loc_conf         /* merge  location config    */
};

ngx_module_t  ngx_http_consul_service_discovery_module = {
    NGX_MODULE_V1,
    &ngx_http_consul_module_ctx,
    ngx_http_consul_commands,
    NGX_HTTP_MODULE,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NGX_MODULE_V1_PADDING
};


/* ═══════════════════════════════════════════════════════════════════════
 *  Config: create / merge
 * ═══════════════════════════════════════════════════════════════════════ */

static void *
ngx_http_consul_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_consul_main_conf_t  *mcf;
    mcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_consul_main_conf_t));
    return mcf;
}

static void *
ngx_http_consul_create_srv_conf(ngx_conf_t *cf)
{
    ngx_http_consul_srv_conf_t  *scf;
    scf = ngx_pcalloc(cf->pool, sizeof(ngx_http_consul_srv_conf_t));
    if (scf == NULL) { return NULL; }
    scf->discoverable  = NGX_CONF_UNSET;
    scf->port_override = NGX_CONF_UNSET;
    /* address_override is ngx_str_t — zero from pcalloc, no extra init needed */
    return scf;
}

static char *
ngx_http_consul_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_consul_srv_conf_t  *prev = parent;
    ngx_http_consul_srv_conf_t  *conf = child;

    ngx_conf_merge_value(conf->discoverable,  prev->discoverable,  0);
    ngx_conf_merge_str_value(conf->name,      prev->name,          "");
    ngx_conf_merge_str_value(conf->id,        prev->id,            "");
    ngx_conf_merge_str_value(conf->tags,      prev->tags,          "nginx");
    ngx_conf_merge_value(conf->port_override, prev->port_override, NGX_CONF_UNSET);
    ngx_conf_merge_str_value(conf->address_override, prev->address_override, "");

    if (conf->meta   == NULL) { conf->meta   = prev->meta;   }
    if (conf->checks == NULL) { conf->checks = prev->checks; }

    return NGX_CONF_OK;
}

static void *
ngx_http_consul_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_consul_loc_conf_t  *lcf;
    lcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_consul_loc_conf_t));
    if (lcf == NULL) { return NULL; }
    lcf->enabled = NGX_CONF_UNSET;
    return lcf;
}

static char *
ngx_http_consul_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_consul_loc_conf_t  *prev = parent;
    ngx_http_consul_loc_conf_t  *conf = child;
    ngx_conf_merge_value(conf->enabled, prev->enabled, 0);
    return NGX_CONF_OK;
}


/* ═══════════════════════════════════════════════════════════════════════
 *  Custom directive handlers
 * ═══════════════════════════════════════════════════════════════════════ */

/*
 * consul_service_discovery on | off;
 *
 * When "on": installs the JSON handler directly on the location's
 * clcf->handler slot — the same pattern used by ngx_http_stub_status_module.
 * This guarantees NGINX calls the handler via the normal location dispatch
 * path and finalises the request correctly.
 */
static char *
ngx_http_consul_discovery_cmd(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_consul_loc_conf_t   *lcf = conf;
    ngx_http_core_loc_conf_t     *clcf;
    ngx_str_t                    *value;

    value = cf->args->elts;

    if (ngx_strcasecmp(value[1].data, (u_char *) "on") == 0) {
        lcf->enabled = 1;
        clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
        clcf->handler = ngx_http_consul_handler;

    } else if (ngx_strcasecmp(value[1].data, (u_char *) "off") == 0) {
        lcf->enabled = 0;

    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "consul_service_discovery: invalid value \"%V\", "
            "expected \"on\" or \"off\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_consul_meta_cmd(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_consul_srv_conf_t  *scf = conf;
    ngx_str_t                   *value;
    ngx_keyval_t                *kv;

    if (scf->meta == NULL) {
        scf->meta = ngx_array_create(cf->pool, 4, sizeof(ngx_keyval_t));
        if (scf->meta == NULL) { return NGX_CONF_ERROR; }
    }

    kv = ngx_array_push(scf->meta);
    if (kv == NULL) { return NGX_CONF_ERROR; }

    value = cf->args->elts;
    kv->key   = value[1];
    kv->value = value[2];

    return NGX_CONF_OK;
}


/*
 * Parse: consul_service_check name=tcp  tcp=10.0.1.5:18880  interval=10s  timeout=1s;
 *        consul_service_check name=web  http=http://host/health  interval=15s  timeout=2s;
 *
 * Each token is "key=value".  The check type is whichever of
 * tcp / http / grpc / ttl appears; its value is the target URL / address.
 */
static char *
ngx_http_consul_check_cmd(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_consul_srv_conf_t  *scf = conf;
    ngx_str_t                   *value;
    ngx_uint_t                   i;
    ngx_http_consul_check_t     *chk;
    ngx_str_t                    key, val;
    u_char                      *eq;

    if (scf->checks == NULL) {
        scf->checks = ngx_array_create(cf->pool, 2,
                                       sizeof(ngx_http_consul_check_t));
        if (scf->checks == NULL) { return NGX_CONF_ERROR; }
    }

    chk = ngx_array_push(scf->checks);
    if (chk == NULL) { return NGX_CONF_ERROR; }
    ngx_memzero(chk, sizeof(ngx_http_consul_check_t));

    value = cf->args->elts;

    for (i = 1; i < cf->args->nelts; i++) {
        eq = ngx_strlchr(value[i].data,
                         value[i].data + value[i].len, '=');
        if (eq == NULL) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "consul_service_check: \"%V\" is not key=value", &value[i]);
            return NGX_CONF_ERROR;
        }
        key.data = value[i].data;
        key.len  = (size_t)(eq - value[i].data);
        val.data = eq + 1;
        val.len  = value[i].len - key.len - 1;

#define KEYCMP(lit) \
        (key.len == sizeof(lit) - 1 && \
         ngx_strncmp(key.data, lit, sizeof(lit) - 1) == 0)

        if      (KEYCMP("name"))     { chk->name     = val; }
        else if (KEYCMP("interval")) { chk->interval  = val; }
        else if (KEYCMP("timeout"))  { chk->timeout   = val; }
        else if (KEYCMP("tcp"))  {
            ngx_str_set(&chk->type, "tcp");
            chk->target = val;
        } else if (KEYCMP("http")) {
            ngx_str_set(&chk->type, "http");
            chk->target = val;
        } else if (KEYCMP("grpc")) {
            ngx_str_set(&chk->type, "grpc");
            chk->target = val;
        } else if (KEYCMP("ttl")) {
            ngx_str_set(&chk->type, "ttl");
            chk->target = val;
        } else {
            ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                "consul_service_check: unknown key \"%V\", ignored", &key);
        }
#undef KEYCMP
    }

    if (chk->type.len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "consul_service_check: check type "
            "(tcp/http/grpc/ttl) not specified");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


/* ═══════════════════════════════════════════════════════════════════════
 *  Port / address auto-discovery (walk cmcf->ports before optimize)
 * ═══════════════════════════════════════════════════════════════════════ */

static ngx_int_t
ngx_http_consul_find_port(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *target, ngx_uint_t *out)
{
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_conf_port_t       *ports;
    ngx_http_conf_addr_t       *addrs;
    ngx_http_core_srv_conf_t  **srv;
    ngx_uint_t                  p, a, s;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    if (cmcf->ports == NULL) { return NGX_ERROR; }

    ports = cmcf->ports->elts;
    for (p = 0; p < cmcf->ports->nelts; p++) {
        addrs = ports[p].addrs.elts;
        for (a = 0; a < ports[p].addrs.nelts; a++) {
            srv = addrs[a].servers.elts;
            for (s = 0; s < addrs[a].servers.nelts; s++) {
                if (srv[s] == target) {
                    *out = ports[p].port;
                    return NGX_OK;
                }
            }
        }
    }
    return NGX_ERROR;
}

static ngx_int_t
ngx_http_consul_find_addr(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *target, ngx_str_t *out)
{
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_conf_port_t       *ports;
    ngx_http_conf_addr_t       *addrs;
    ngx_http_core_srv_conf_t  **srv;
    ngx_uint_t                  p, a, s;
    u_char                     *buf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    if (cmcf->ports == NULL) { return NGX_ERROR; }

    ports = cmcf->ports->elts;
    for (p = 0; p < cmcf->ports->nelts; p++) {
        addrs = ports[p].addrs.elts;
        for (a = 0; a < ports[p].addrs.nelts; a++) {
            srv = addrs[a].servers.elts;
            for (s = 0; s < addrs[a].servers.nelts; s++) {
                if (srv[s] == target) {
                    buf = ngx_pcalloc(cf->pool, NGX_SOCKADDR_STRLEN);
                    if (buf == NULL) { return NGX_ERROR; }
                    out->len  = ngx_sock_ntop(addrs[a].opt.sockaddr,
                                              addrs[a].opt.socklen,
                                              buf, NGX_SOCKADDR_STRLEN, 0);
                    out->data = buf;
                    return NGX_OK;
                }
            }
        }
    }
    return NGX_ERROR;
}


/* ═══════════════════════════════════════════════════════════════════════
 *  Tag-splitting helper  ("v1,public, http" → ["v1","public","http"])
 * ═══════════════════════════════════════════════════════════════════════ */

static ngx_array_t *
ngx_http_consul_split_tags(ngx_pool_t *pool, ngx_str_t *csv)
{
    ngx_array_t  *arr;
    ngx_str_t    *tag;
    u_char       *start, *p, *end, *ts, *te;

    arr = ngx_array_create(pool, 4, sizeof(ngx_str_t));
    if (arr == NULL || csv->len == 0) { return arr; }

    start = csv->data;
    end   = csv->data + csv->len;

    for (p = start; p <= end; p++) {
        if (p == end || *p == ',') {
            ts = start; te = p;
            while (ts < te && *ts == ' ') { ts++; }
            while (te > ts && *(te - 1) == ' ') { te--; }
            if (te > ts) {
                tag = ngx_array_push(arr);
                if (tag == NULL) { return NULL; }
                tag->data = ts;
                tag->len  = (size_t)(te - ts);
            }
            start = p + 1;
        }
    }
    return arr;
}


/* ═══════════════════════════════════════════════════════════════════════
 *  JSON string escape helpers
 * ═══════════════════════════════════════════════════════════════════════ */

/* Return the number of decimal digits needed to print n */
static size_t
ngx_http_consul_int_len(ngx_int_t n)
{
    size_t  len = 1;
    if (n < 0) { len++; n = -n; }
    while (n >= 10) { len++; n /= 10; }
    return len;
}

/* Return the byte-length of s when JSON-escaped (without surrounding quotes) */
static size_t
ngx_http_consul_escape_len(ngx_str_t *s)
{
    size_t  i, n = 0;
    u_char  c;
    for (i = 0; i < s->len; i++) {
        c = s->data[i];
        if (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t') {
            n += 2;
        } else if (c < 0x20) {
            n += 6;     /* \uXXXX */
        } else {
            n++;
        }
    }
    return n;
}

/* Write JSON-escaped content of s to dst; return advanced pointer */
static u_char *
ngx_http_consul_escape(u_char *dst, ngx_str_t *s)
{
    size_t  i;
    u_char  c;
    for (i = 0; i < s->len; i++) {
        c = s->data[i];
        switch (c) {
        case '"':  *dst++ = '\\'; *dst++ = '"';  break;
        case '\\': *dst++ = '\\'; *dst++ = '\\'; break;
        case '\n': *dst++ = '\\'; *dst++ = 'n';  break;
        case '\r': *dst++ = '\\'; *dst++ = 'r';  break;
        case '\t': *dst++ = '\\'; *dst++ = 't';  break;
        default:
            if (c < 0x20) {
                dst = ngx_sprintf(dst, "\\u%04xd", (unsigned) c);
            } else {
                *dst++ = c;
            }
        }
    }
    return dst;
}

/* Write a quoted JSON string: "..." */
#define JSTR(dst, s) \
    do { *(dst)++ = '"'; (dst) = ngx_http_consul_escape((dst), (s)); \
         *(dst)++ = '"'; } while (0)

/* Byte-length of a quoted JSON string */
#define JSTR_LEN(s)  (2 + ngx_http_consul_escape_len(s))


/* ═══════════════════════════════════════════════════════════════════════
 *  JSON size calculation  (exact)
 * ═══════════════════════════════════════════════════════════════════════ */

static size_t
ngx_http_consul_json_len(ngx_http_consul_main_conf_t *mcf)
{
    ngx_http_consul_service_t   *svcs;
    ngx_http_consul_check_t     *chks;
    ngx_keyval_t                *kvs;
    ngx_str_t                   *tags;
    ngx_uint_t                   i, j;
    size_t                       n;

    /* {"services":[  ]}  */
    n = sizeof("{\"services\":[]}") - 1;

    svcs = mcf->services->elts;
    for (i = 0; i < mcf->services->nelts; i++) {
        ngx_http_consul_service_t *sv = &svcs[i];

        if (i > 0) { n++; }   /* , */

        /* { */
        n++;

        /* "id":"...",  */
        n += sizeof("\"id\":") - 1 + JSTR_LEN(&sv->id) + 1;

        /* "name":"...", */
        n += sizeof("\"name\":") - 1 + JSTR_LEN(&sv->name) + 1;

        /* "address":"...", */
        n += sizeof("\"address\":") - 1 + JSTR_LEN(&sv->address) + 1;

        /* "port":NNNNN, */
        n += sizeof("\"port\":") - 1 + ngx_http_consul_int_len(sv->port) + 1;

        /* "tags":[...]  */
        n += sizeof("\"tags\":[") - 1;
        tags = sv->tags->elts;
        for (j = 0; j < sv->tags->nelts; j++) {
            if (j > 0) { n++; }
            n += JSTR_LEN(&tags[j]);
        }
        n++;    /* ] */

        /* "meta":{...} */
        if (sv->meta && sv->meta->nelts > 0) {
            n += sizeof(",\"meta\":{") - 1;
            kvs = sv->meta->elts;
            for (j = 0; j < sv->meta->nelts; j++) {
                if (j > 0) { n++; }
                n += JSTR_LEN(&kvs[j].key) + 1 + JSTR_LEN(&kvs[j].value);
            }
            n++;    /* } */
        }

        /* "checks":[...] */
        if (sv->checks && sv->checks->nelts > 0) {
            n += sizeof(",\"checks\":[") - 1;
            chks = sv->checks->elts;
            for (j = 0; j < sv->checks->nelts; j++) {
                ngx_http_consul_check_t *ck = &chks[j];
                if (j > 0) { n++; }
                n++;    /* { */

                /* "name":"..." */
                n += sizeof("\"name\":") - 1 + JSTR_LEN(&ck->name);

                /* ,"tcp":"..." | ,"http":"..." … */
                n += 2 + JSTR_LEN(&ck->type) + 1 + JSTR_LEN(&ck->target);

                if (ck->interval.len) {
                    n += sizeof(",\"interval\":") - 1 + JSTR_LEN(&ck->interval);
                }
                if (ck->timeout.len) {
                    n += sizeof(",\"timeout\":") - 1 + JSTR_LEN(&ck->timeout);
                }
                n++;    /* } */
            }
            n++;    /* ] */
        }

        /* } */
        n++;
    }

    return n;
}


/* ═══════════════════════════════════════════════════════════════════════
 *  JSON fill  (must mirror ngx_http_consul_json_len exactly)
 * ═══════════════════════════════════════════════════════════════════════ */

static u_char *
ngx_http_consul_json_fill(u_char *p, ngx_http_consul_main_conf_t *mcf)
{
    ngx_http_consul_service_t   *svcs;
    ngx_http_consul_check_t     *chks;
    ngx_keyval_t                *kvs;
    ngx_str_t                   *tags;
    ngx_uint_t                   i, j;

    p = ngx_cpymem(p, "{\"services\":[", sizeof("{\"services\":[") - 1);

    svcs = mcf->services->elts;
    for (i = 0; i < mcf->services->nelts; i++) {
        ngx_http_consul_service_t *sv = &svcs[i];

        if (i > 0) { *p++ = ','; }

        *p++ = '{';

        /* "id":"..." */
        p = ngx_cpymem(p, "\"id\":", 5);
        JSTR(p, &sv->id);
        *p++ = ',';

        /* "name":"..." */
        p = ngx_cpymem(p, "\"name\":", 7);
        JSTR(p, &sv->name);
        *p++ = ',';

        /* "address":"..." */
        p = ngx_cpymem(p, "\"address\":", 10);
        JSTR(p, &sv->address);
        *p++ = ',';

        /* "port":NNN */
        p = ngx_sprintf(p, "\"port\":%i,", sv->port);

        /* "tags":[...] */
        p = ngx_cpymem(p, "\"tags\":[", 8);
        tags = sv->tags->elts;
        for (j = 0; j < sv->tags->nelts; j++) {
            if (j > 0) { *p++ = ','; }
            JSTR(p, &tags[j]);
        }
        *p++ = ']';

        /* "meta":{...} */
        if (sv->meta && sv->meta->nelts > 0) {
            p = ngx_cpymem(p, ",\"meta\":{", 9);
            kvs = sv->meta->elts;
            for (j = 0; j < sv->meta->nelts; j++) {
                if (j > 0) { *p++ = ','; }
                JSTR(p, &kvs[j].key);
                *p++ = ':';
                JSTR(p, &kvs[j].value);
            }
            *p++ = '}';
        }

        /* "checks":[...] */
        if (sv->checks && sv->checks->nelts > 0) {
            p = ngx_cpymem(p, ",\"checks\":[", 11);
            chks = sv->checks->elts;
            for (j = 0; j < sv->checks->nelts; j++) {
                ngx_http_consul_check_t *ck = &chks[j];
                if (j > 0) { *p++ = ','; }
                *p++ = '{';

                /* "name":"..." */
                p = ngx_cpymem(p, "\"name\":", 7);
                JSTR(p, &ck->name);

                /* ,"tcp":"10.0.0.1:80" — the key is the check type */
                *p++ = ',';
                JSTR(p, &ck->type);
                *p++ = ':';
                JSTR(p, &ck->target);

                if (ck->interval.len) {
                    p = ngx_cpymem(p, ",\"interval\":", 12);
                    JSTR(p, &ck->interval);
                }
                if (ck->timeout.len) {
                    p = ngx_cpymem(p, ",\"timeout\":", 11);
                    JSTR(p, &ck->timeout);
                }

                *p++ = '}';
            }
            *p++ = ']';
        }

        *p++ = '}';
    }

    p = ngx_cpymem(p, "]}", 2);
    return p;
}


/* ═══════════════════════════════════════════════════════════════════════
 *  Request handler
 * ═══════════════════════════════════════════════════════════════════════ */

static ngx_int_t
ngx_http_consul_handler(ngx_http_request_t *r)
{
    ngx_http_consul_main_conf_t *mcf;
    ngx_buf_t                   *b;
    ngx_chain_t                  out;
    size_t                       len;
    ngx_int_t                    rc;

    if (!(r->method & (NGX_HTTP_GET | NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        ngx_http_finalize_request(r, rc);
        return NGX_DONE;
    }

    mcf = ngx_http_get_module_main_conf(r,
              ngx_http_consul_service_discovery_module);

    if (mcf->services == NULL || mcf->services->nelts == 0) {
        /* return {"services":[]} */
        static u_char empty[] = "{\"services\":[]}";
        b = ngx_calloc_buf(r->pool);
        if (b == NULL) { return NGX_HTTP_INTERNAL_SERVER_ERROR; }
        b->pos           = empty;
        b->last          = empty + sizeof(empty) - 1;
        b->memory        = 1;
        b->last_buf      = 1;
        b->last_in_chain = 1;
        len = sizeof(empty) - 1;
    } else {
        /*
         * ngx_http_consul_json_len() is a safe upper bound; the fill may
         * write fewer bytes (integer widths, escape sequences, etc.).
         * ALWAYS derive Content-Length from b->last - b->pos after the fill
         * so the two values cannot diverge.
         */
        b = ngx_create_temp_buf(r->pool, ngx_http_consul_json_len(mcf));
        if (b == NULL) { return NGX_HTTP_INTERNAL_SERVER_ERROR; }
        b->last          = ngx_http_consul_json_fill(b->pos, mcf);
        b->last_buf      = 1;
        b->last_in_chain = 1;
        len = (size_t)(b->last - b->pos);   /* actual bytes, not the estimate */
    }

    r->headers_out.status           = NGX_HTTP_OK;
    r->headers_out.content_length_n = (off_t) len;
    ngx_str_set(&r->headers_out.content_type, "application/json");
    r->headers_out.content_type_len = r->headers_out.content_type.len;

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        ngx_http_finalize_request(r, rc);
        return NGX_DONE;
    }

    out.buf  = b;
    out.next = NULL;

    rc = ngx_http_output_filter(r, &out);
    ngx_http_finalize_request(r, rc);
    return NGX_DONE;
}


/* ═══════════════════════════════════════════════════════════════════════
 *  Postconfiguration – install handler + build catalogue
 * ═══════════════════════════════════════════════════════════════════════ */

static ngx_int_t
ngx_http_consul_postconfiguration(ngx_conf_t *cf)
{
    ngx_http_core_main_conf_t   *cmcf;
    ngx_http_core_srv_conf_t   **cscfp;
    ngx_http_consul_main_conf_t *mcf;
    ngx_http_consul_srv_conf_t  *scf;
    ngx_http_consul_service_t   *svc;
    ngx_http_server_name_t      *sn;
    ngx_uint_t                   i, port;
    ngx_str_t                    addr, first_tag;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    mcf  = ngx_http_conf_get_module_main_conf(cf,
               ngx_http_consul_service_discovery_module);

    mcf->services = ngx_array_create(cf->pool, 8,
                                     sizeof(ngx_http_consul_service_t));
    if (mcf->services == NULL) { return NGX_ERROR; }

    cscfp = cmcf->servers.elts;
    for (i = 0; i < cmcf->servers.nelts; i++) {
        scf = ngx_http_get_module_srv_conf(cscfp[i]->ctx,
                  ngx_http_consul_service_discovery_module);
        if (!scf->discoverable) { continue; }

        svc = ngx_array_push(mcf->services);
        if (svc == NULL) { return NGX_ERROR; }
        ngx_memzero(svc, sizeof(ngx_http_consul_service_t));

        /* ── name ────────────────────────────────────────────────────── */
        if (scf->name.len) {
            svc->name = scf->name;
        } else if (cscfp[i]->server_names.nelts) {
            sn = cscfp[i]->server_names.elts;
            svc->name = sn[0].name;
        } else {
            ngx_str_set(&svc->name, "unknown");
        }

        /* ── tags (CSV → array) ──────────────────────────────────────── */
        svc->tags = ngx_http_consul_split_tags(cf->pool, &scf->tags);
        if (svc->tags == NULL) { return NGX_ERROR; }

        /* ── id ──────────────────────────────────────────────────────── */
        if (scf->id.len) {
            svc->id = scf->id;
        } else if (svc->tags->nelts > 0) {
            first_tag = ((ngx_str_t *) svc->tags->elts)[0];
            svc->id.len  = svc->name.len + 1 + first_tag.len;
            svc->id.data = ngx_pnalloc(cf->pool, svc->id.len);
            if (svc->id.data == NULL) { return NGX_ERROR; }
            ngx_snprintf(svc->id.data, svc->id.len,
                         "%V-%V", &svc->name, &first_tag);
        } else {
            svc->id = svc->name;
        }

        /* ── meta ────────────────────────────────────────────────────── */
        svc->meta = scf->meta;

        /* ── checks ──────────────────────────────────────────────────── */
        svc->checks = scf->checks;

        /* ── port ────────────────────────────────────────────────────── */
        if (scf->port_override != NGX_CONF_UNSET) {
            svc->port = scf->port_override;
        } else if (ngx_http_consul_find_port(cf, cscfp[i], &port) == NGX_OK) {
            svc->port = (ngx_int_t) port;
        } else {
            ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                "consul: cannot determine port for service \"%V\" — "
                "add listen or consul_service_port_override", &svc->name);
            svc->port = 0;
        }

        /* ── address ─────────────────────────────────────────────────── */
        if (scf->address_override.len) {
            svc->address = scf->address_override;
        } else if (ngx_http_consul_find_addr(cf, cscfp[i], &addr) == NGX_OK) {
            svc->address = addr;
        } else {
            ngx_str_set(&svc->address, "0.0.0.0");
        }
    }

    return NGX_OK;
}
