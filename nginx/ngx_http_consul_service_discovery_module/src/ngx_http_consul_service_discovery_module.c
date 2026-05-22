/*
 * ngx_http_consul_service_discovery_module.c
 *
 * NGINX module that exposes server{} blocks tagged with
 * consul_service_discoverable as a JSON service catalogue via a configurable
 * location endpoint.  It does NOT talk to Consul directly; a sidecar (or
 * Consul's own agent) can poll the endpoint and register/deregister services.
 *
 * Port and address auto-discovery
 * ────────────────────────────────
 * At postconfiguration time the module walks cmcf->ports, which is the array
 * of ngx_http_conf_port_t entries built by the core module from every listen
 * directive.  All postconfiguration hooks run before ngx_http_optimize_servers
 * (ngx_http.c line 309 vs 335), so the temp-pool arrays are still live.
 *
 * For each discoverable server{} block ngx_http_consul_find_listen_info():
 *
 *   1. Scans cmcf->ports → addrs → servers[] by pointer equality against the
 *      target ngx_http_core_srv_conf_t *.
 *
 *   2. Reads the matched ngx_http_conf_addr_t.opt.sockaddr with
 *      ngx_sock_ntop(..., port=0) to get the bind IP ("0.0.0.0", "1.2.3.4",
 *      "::", etc.) without the port suffix.  Buffer is allocated on cf->pool
 *      (permanent pool) and thus valid for the worker lifetime.
 *
 *   3. Reads ngx_http_conf_port_t.port — stored in host byte order because
 *      ngx_inet_get_port calls ntohs before storing it.
 *
 * consul_service_port_override overrides the discovered port for NAT /
 * load-balancer deployments.  The address is always auto-discovered from the
 * listen directive; there is no address override directive.
 *
 * Copyright (c) 2024 – present, Contributors
 * Licensed under the MIT License.  See LICENSE for details.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include "ngx_http_consul_service_discovery.h"


/* ── forward declarations ─────────────────────────────────────────────────── */

static ngx_int_t ngx_http_consul_service_discovery_postconfiguration(
    ngx_conf_t *cf);
static ngx_int_t ngx_http_consul_service_discovery_collect_servers(
    ngx_conf_t *cf);

/*
 * Fills srv->addr and srv->port from the listen directive of target_cscf.
 * Returns NGX_OK on success (even if port is 0 — a warning is logged).
 * Returns NGX_ERROR only on allocation failure.
 */
static ngx_int_t ngx_http_consul_find_listen_info(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *target_cscf,
    ngx_http_consul_service_t *srv);

static ngx_int_t ngx_http_consul_service_discovery_handler(
    ngx_http_request_t *r);

static void *ngx_http_consul_service_discovery_create_main_conf(
    ngx_conf_t *cf);
static void *ngx_http_consul_service_discovery_create_srv_conf(
    ngx_conf_t *cf);
static char *ngx_http_consul_service_discovery_merge_srv_conf(
    ngx_conf_t *cf, void *parent, void *child);
static void *ngx_http_consul_service_discovery_create_loc_conf(
    ngx_conf_t *cf);
static char *ngx_http_consul_service_discovery_merge_loc_conf(
    ngx_conf_t *cf, void *parent, void *child);


/* ── directive table ──────────────────────────────────────────────────────── */

static ngx_command_t  ngx_http_consul_service_discovery_commands[] = {

    /*
     * consul_service_discovery on|off;
     * Context: location
     */
    { ngx_string("consul_service_discovery"),
      NGX_HTTP_LOC_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_consul_service_discovery_loc_conf_t, enable),
      NULL },

    /*
     * consul_service_discoverable on|off;
     * Context: server
     */
    { ngx_string("consul_service_discoverable"),
      NGX_HTTP_SRV_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_consul_service_discovery_srv_conf_t, discoverable),
      NULL },

    /*
     * consul_service_name "<name>";
     * Context: server
     * Defaults to the first server_name value when omitted.
     */
    { ngx_string("consul_service_name"),
      NGX_HTTP_SRV_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_consul_service_discovery_srv_conf_t, service_name),
      NULL },

    /*
     * consul_service_tags "<tag1,tag2,...>";
     * Context: server
     */
    { ngx_string("consul_service_tags"),
      NGX_HTTP_SRV_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_consul_service_discovery_srv_conf_t, tags),
      NULL },

    /*
     * consul_service_port_override <port>;
     * Context: server
     * Optional.  Overrides the auto-discovered listen port.  Use only when
     * the external port consumers connect to differs from the NGINX listen
     * port (NAT / load-balancer).  The address is always auto-discovered.
     */
    { ngx_string("consul_service_port_override"),
      NGX_HTTP_SRV_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_consul_service_discovery_srv_conf_t, port_override),
      NULL },

    ngx_null_command
};


/* ── module context ───────────────────────────────────────────────────────── */

static ngx_http_module_t  ngx_http_consul_service_discovery_module_ctx = {
    NULL,                                                   /* preconfiguration  */
    ngx_http_consul_service_discovery_postconfiguration,   /* postconfiguration */

    ngx_http_consul_service_discovery_create_main_conf,    /* create main conf  */
    NULL,                                                   /* init main conf    */

    ngx_http_consul_service_discovery_create_srv_conf,     /* create srv conf   */
    ngx_http_consul_service_discovery_merge_srv_conf,      /* merge  srv conf   */

    ngx_http_consul_service_discovery_create_loc_conf,     /* create loc conf   */
    ngx_http_consul_service_discovery_merge_loc_conf       /* merge  loc conf   */
};


/* ── module definition ────────────────────────────────────────────────────── */

ngx_module_t  ngx_http_consul_service_discovery_module = {
    NGX_MODULE_V1,
    &ngx_http_consul_service_discovery_module_ctx,
    ngx_http_consul_service_discovery_commands,
    NGX_HTTP_MODULE,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NGX_MODULE_V1_PADDING
};


/* ── configuration lifecycle ──────────────────────────────────────────────── */

static void *
ngx_http_consul_service_discovery_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_consul_service_discovery_main_conf_t  *mcf;

    mcf = ngx_pcalloc(cf->pool,
              sizeof(ngx_http_consul_service_discovery_main_conf_t));
    if (mcf == NULL) {
        return NULL;
    }

    mcf->services = NULL;
    mcf->updated  = 0;

    return mcf;
}


static void *
ngx_http_consul_service_discovery_create_srv_conf(ngx_conf_t *cf)
{
    ngx_http_consul_service_discovery_srv_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool,
               sizeof(ngx_http_consul_service_discovery_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->discoverable  = NGX_CONF_UNSET;
    ngx_str_null(&conf->service_name);
    ngx_str_null(&conf->tags);
    conf->port_override = NGX_CONF_UNSET_UINT;

    return conf;
}


static char *
ngx_http_consul_service_discovery_merge_srv_conf(ngx_conf_t *cf,
    void *parent, void *child)
{
    ngx_http_consul_service_discovery_srv_conf_t  *prev = parent;
    ngx_http_consul_service_discovery_srv_conf_t  *conf = child;

    ngx_conf_merge_value(conf->discoverable, prev->discoverable, 0);
    ngx_conf_merge_str_value(conf->service_name, prev->service_name, "");
    ngx_conf_merge_str_value(conf->tags, prev->tags, "");
    ngx_conf_merge_uint_value(conf->port_override, prev->port_override, 0);

    return NGX_CONF_OK;
}


static void *
ngx_http_consul_service_discovery_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_consul_service_discovery_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool,
               sizeof(ngx_http_consul_service_discovery_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->enable = NGX_CONF_UNSET;

    return conf;
}


static char *
ngx_http_consul_service_discovery_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child)
{
    ngx_http_consul_service_discovery_loc_conf_t  *prev = parent;
    ngx_http_consul_service_discovery_loc_conf_t  *conf = child;

    ngx_conf_merge_value(conf->enable, prev->enable, 0);

    return NGX_CONF_OK;
}


/* ── listen-directive address and port discovery ──────────────────────────── */

/*
 * ngx_http_consul_find_listen_info()
 *
 * Walk cmcf->ports → addrs → servers[] to find the listen entry for
 * target_cscf, then populate srv->addr and srv->port.
 *
 * ADDRESS
 *   Taken from the matched ngx_http_conf_addr_t.opt.sockaddr using
 *   ngx_sock_ntop(..., port=0), which gives the bare IP with no port suffix:
 *
 *     listen 80;           → "0.0.0.0"
 *     listen 0.0.0.0:80;   → "0.0.0.0"
 *     listen 1.2.3.4:80;   → "1.2.3.4"
 *     listen [::]:80;      → "::"
 *     listen [::1]:443;    → "::1"
 *
 *   A permanent-pool buffer (NGX_SOCKADDR_STRLEN bytes) is allocated for the
 *   text so srv->addr.data remains valid for the worker lifetime.
 *
 * PORT
 *   ngx_http_conf_port_t.port is in host byte order — ngx_inet_get_port calls
 *   ntohs before storing it (ngx_http.c:ngx_http_add_listen).
 *
 * MULTIPLE LISTEN DIRECTIVES
 *   When a server{} block has more than one listen directive the first match
 *   found by the port-then-address scan is used.  Set consul_service_port_override
 *   to select a specific port when this matters.
 *
 * RETURN VALUE
 *   NGX_OK    — addr and port filled (port may be 0 if no listen found;
 *               a [warn] is emitted in that case)
 *   NGX_ERROR — ngx_pnalloc failed
 */
static ngx_int_t
ngx_http_consul_find_listen_info(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *target_cscf,
    ngx_http_consul_service_t *srv)
{
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_conf_port_t       *ports;
    ngx_http_conf_addr_t       *addrs;
    ngx_http_core_srv_conf_t  **srvp;
    u_char                     *buf;
    size_t                      len;
    ngx_uint_t                  p, a, s;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    if (cmcf->ports == NULL) {
        goto not_found;
    }

    ports = cmcf->ports->elts;

    for (p = 0; p < cmcf->ports->nelts; p++) {

        if (ports[p].addrs.elts == NULL) {
            continue;
        }

        addrs = ports[p].addrs.elts;

        for (a = 0; a < ports[p].addrs.nelts; a++) {

            if (addrs[a].servers.elts == NULL) {
                continue;
            }

            srvp = addrs[a].servers.elts;

            for (s = 0; s < addrs[a].servers.nelts; s++) {

                if (srvp[s] != target_cscf) {
                    continue;
                }

                /*
                 * Found the matching listen entry.
                 *
                 * PORT — already in host byte order.
                 */
                srv->port = (ngx_uint_t) ports[p].port;

                /*
                 * ADDRESS — call ngx_sock_ntop with port=0 to get the bare
                 * IP address without the ":port" suffix that addr_text
                 * contains (addr_text was built with port=1).
                 *
                 * Allocate on cf->pool (permanent pool) so the data pointer
                 * outlives cf->temp_pool and remains valid at request time.
                 */
                buf = ngx_pnalloc(cf->pool, NGX_SOCKADDR_STRLEN);
                if (buf == NULL) {
                    return NGX_ERROR;
                }

                len = ngx_sock_ntop(addrs[a].opt.sockaddr,
                                    addrs[a].opt.socklen,
                                    buf, NGX_SOCKADDR_STRLEN,
                                    0 /* port=0: IP only, no ":port" */);

                srv->addr.data = buf;
                srv->addr.len  = len;

                return NGX_OK;
            }
        }
    }

not_found:

    ngx_log_error(NGX_LOG_WARN, cf->log, 0,
        "consul_service_discovery: no listen entry found for server \"%V\"; "
        "set consul_service_port_override to suppress this warning",
        &srv->name);

    ngx_str_set(&srv->addr, "");
    srv->port = 0;

    return NGX_OK;
}


/* ── server collection ────────────────────────────────────────────────────── */

static ngx_int_t
ngx_http_consul_service_discovery_collect_servers(ngx_conf_t *cf)
{
    ngx_http_consul_service_discovery_main_conf_t  *mcf;
    ngx_http_core_main_conf_t                      *cmcf;
    ngx_http_core_srv_conf_t                      **cscfp;
    ngx_http_consul_service_discovery_srv_conf_t   *scf;
    ngx_http_consul_service_t                      *srv;
    ngx_uint_t                                      i, n;

    static const ngx_str_t default_tags = ngx_string("nginx");

    mcf  = ngx_http_conf_get_module_main_conf(cf,
               ngx_http_consul_service_discovery_module);
    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    if (mcf->services == NULL) {
        mcf->services = ngx_array_create(cf->pool, 8,
                            sizeof(ngx_http_consul_service_t));
        if (mcf->services == NULL) {
            return NGX_ERROR;
        }
    }

    cscfp = cmcf->servers.elts;
    n     = cmcf->servers.nelts;

    for (i = 0; i < n; i++) {

        scf = ngx_http_conf_get_module_srv_conf(cscfp[i],
                  ngx_http_consul_service_discovery_module);

        if (scf == NULL || !scf->discoverable) {
            continue;
        }

        srv = ngx_array_push(mcf->services);
        if (srv == NULL) {
            return NGX_ERROR;
        }
        ngx_memzero(srv, sizeof(ngx_http_consul_service_t));

        /*
         * Service name: consul_service_name if set, else first server_name.
         * cscf->server_name is populated by the core's merge_srv_conf, which
         * runs at ngx_http.c line 270 — before our postconfiguration at
         * line 309.
         */
        srv->name = (scf->service_name.len > 0)
                    ? scf->service_name
                    : cscfp[i]->server_name;

        /*
         * Address and port: auto-discovered from the listen directive.
         * consul_service_port_override overrides the port only.
         */
        if (ngx_http_consul_find_listen_info(cf, cscfp[i], srv) != NGX_OK) {
            return NGX_ERROR;
        }

        if (scf->port_override != NGX_CONF_UNSET_UINT
            && scf->port_override != 0)
        {
            srv->port = scf->port_override;
        }

        /* Tags: consul_service_tags if set, else the default "nginx". */
        srv->tags = (scf->tags.len > 0) ? scf->tags : default_tags;
    }

    mcf->updated++;
    return NGX_OK;
}


/* ── postconfiguration ────────────────────────────────────────────────────── */

static ngx_int_t
ngx_http_consul_service_discovery_postconfiguration(ngx_conf_t *cf)
{
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_handler_pt        *h;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_CONTENT_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_consul_service_discovery_handler;

    return ngx_http_consul_service_discovery_collect_servers(cf);
}


/* ── request handler ──────────────────────────────────────────────────────── */

/*
 * Serve GET|HEAD as application/json.
 *
 * Example output:
 *   {"services":[
 *     {"name":"api1","address":"0.0.0.0","port":80,"tags":"v1,public"},
 *     {"name":"api2","address":"1.2.3.4","port":443,"tags":"v2,https"}
 *   ]}
 */
static ngx_int_t
ngx_http_consul_service_discovery_handler(ngx_http_request_t *r)
{
    ngx_http_consul_service_discovery_loc_conf_t  *lcf;
    ngx_http_consul_service_discovery_main_conf_t *mcf;
    ngx_http_consul_service_t                     *services;
    ngx_buf_t                                     *b;
    ngx_chain_t                                    out;
    ngx_uint_t                                     i, n;
    size_t                                         buf_size;

    lcf = ngx_http_get_module_loc_conf(r,
              ngx_http_consul_service_discovery_module);

    if (lcf == NULL || !lcf->enable) {
        return NGX_DECLINED;
    }

    if (r->method != NGX_HTTP_GET && r->method != NGX_HTTP_HEAD) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    mcf      = ngx_http_get_module_main_conf(r,
                   ngx_http_consul_service_discovery_module);
    services = (mcf->services != NULL) ? mcf->services->elts : NULL;
    n        = (mcf->services != NULL) ? mcf->services->nelts : 0;

    /*
     * Buffer: 32 bytes base + 80 bytes JSON scaffolding per entry +
     * variable name/addr/tags lengths.
     */
    buf_size = 32;
    for (i = 0; i < n; i++) {
        buf_size += 80
                    + services[i].name.len
                    + services[i].addr.len
                    + services[i].tags.len;
    }

    b = ngx_create_temp_buf(r->pool, buf_size);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->last = ngx_cpymem(b->last,
                  "{\"services\":[",
                  sizeof("{\"services\":[") - 1);

    for (i = 0; i < n; i++) {
        if (i > 0) {
            *b->last++ = ',';
        }

        b->last = ngx_snprintf(b->last, (size_t)(b->end - b->last),
            "{\"name\":\"%V\",\"address\":\"%V\",\"port\":%ui,\"tags\":\"%V\"}",
            &services[i].name,
            &services[i].addr,
            services[i].port,
            &services[i].tags);
    }

    b->last = ngx_cpymem(b->last, "]}", sizeof("]}") - 1);

    b->last_buf      = 1;
    b->last_in_chain = 1;

    r->headers_out.status            = NGX_HTTP_OK;
    r->headers_out.content_type.len  = sizeof("application/json") - 1;
    r->headers_out.content_type.data = (u_char *) "application/json";
    r->headers_out.content_length_n  = b->last - b->pos;

    ngx_http_send_header(r);

    if (r->method == NGX_HTTP_HEAD) {
        return NGX_OK;
    }

    out.buf  = b;
    out.next = NULL;

    return ngx_http_output_filter(r, &out);
}
