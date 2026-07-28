#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>
#include "ngx_dns_parser.h"

extern ngx_module_t ngx_stream_dns_module;

typedef struct {
    ngx_flag_t   enable;
    size_t       max_size;
} ngx_stream_dns_srv_conf_t;

typedef struct {
    ngx_dns_packet_t req_pkt;
    ngx_uint_t       req_parsed;
    ngx_uint_t       req_parse_ok;

    ngx_dns_packet_t resp_pkt;
    ngx_uint_t       resp_parsed;
    ngx_uint_t       resp_parse_ok;
} ngx_stream_dns_ctx_t;

static void *ngx_stream_dns_create_srv_conf(ngx_conf_t *cf);
static char *ngx_stream_dns_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_stream_dns_add_variables(ngx_conf_t *cf);
static ngx_int_t ngx_stream_dns_init(ngx_conf_t *cf);
static char *ngx_stream_dns_parse_directive(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_stream_dns_preread_handler(ngx_stream_session_t *s);
static ngx_int_t ngx_stream_dns_filter(ngx_stream_session_t *s, ngx_chain_t *in, ngx_uint_t from_upstream);

static ngx_stream_filter_pt next_filter;

/* Stream Preread Phase Handler (Downstream Request Preread) */
static ngx_int_t
ngx_stream_dns_preread_handler(ngx_stream_session_t *s)
{
    ngx_stream_dns_srv_conf_t *scf;
    ngx_stream_dns_ctx_t      *ctx;
    ngx_connection_t          *c;
    ngx_buf_t                 *b;

    scf = ngx_stream_get_module_srv_conf(s, ngx_stream_dns_module);
    if (scf == NULL || !scf->enable) {
        return NGX_DECLINED;
    }

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_dns_module);
    if (ctx == NULL) {
        ctx = ngx_pcalloc(s->connection->pool, sizeof(ngx_stream_dns_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }
        ngx_stream_set_ctx(s, ctx, ngx_stream_dns_module);
    }

    if (ctx->req_parsed) {
        return NGX_DECLINED;
    }

    c = s->connection;
    if (c == NULL) {
        return NGX_DECLINED;
    }

    if (c->buffer != NULL && (size_t)(c->buffer->last - c->buffer->pos) >= 12) {
        b = c->buffer;
        size_t len = (size_t)(b->last - b->pos);
        if (scf->max_size > 0 && len > scf->max_size) {
            len = scf->max_size;
        }

        int is_tcp = (c->type == SOCK_STREAM);
        if (ngx_dns_parse_packet((const uint8_t *)b->pos, len, is_tcp, &ctx->req_pkt) == 0) {
            ctx->req_parse_ok = 1;
            ctx->req_parsed = 1;
        }
        return NGX_DECLINED;
    }

    /* For UDP sockets, do not block or wait with NGX_AGAIN */
    if (c->type == SOCK_DGRAM) {
        return NGX_DECLINED;
    }

    return NGX_AGAIN;
}

/* Stream Body Filter (Live Stream Request/Response Capture) */
static ngx_int_t
ngx_stream_dns_filter(ngx_stream_session_t *s, ngx_chain_t *in, ngx_uint_t from_upstream)
{
    ngx_stream_dns_srv_conf_t *scf;
    ngx_stream_dns_ctx_t      *ctx;
    ngx_chain_t               *cl;

    scf = ngx_stream_get_module_srv_conf(s, ngx_stream_dns_module);
    if (scf == NULL || !scf->enable) {
        return next_filter(s, in, from_upstream);
    }

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_dns_module);
    if (ctx == NULL) {
        ctx = ngx_pcalloc(s->connection->pool, sizeof(ngx_stream_dns_ctx_t));
        if (ctx == NULL) {
            return next_filter(s, in, from_upstream);
        }
        ngx_stream_set_ctx(s, ctx, ngx_stream_dns_module);
    }

    if (in != NULL) {
        size_t max_buf_size = (scf->max_size > 0) ? scf->max_size : 4096;
        u_char *buf = ngx_pnalloc(s->connection->pool, max_buf_size);
        if (buf == NULL) {
            return next_filter(s, in, from_upstream);
        }

        size_t len = 0;

        for (cl = in; cl != NULL && len < max_buf_size; cl = cl->next) {
            if (cl->buf != NULL) {
                size_t size = (size_t)(cl->buf->last - cl->buf->pos);
                if (size > 0) {
                    size_t copy = (size + len <= max_buf_size) ? size : max_buf_size - len;
                    ngx_memcpy(buf + len, cl->buf->pos, copy);
                    len += copy;
                }
            }
        }

        if (len >= 12) {
            int is_tcp = (s->connection && s->connection->type == SOCK_STREAM);
            if (from_upstream) {
                /* Response stream from Upstream server */
                if (!ctx->resp_parsed) {
                    if (ngx_dns_parse_packet((const uint8_t *)buf, len, is_tcp, &ctx->resp_pkt) == 0) {
                        ctx->resp_parse_ok = 1;
                        ctx->resp_parsed = 1;
                    }
                }
            } else {
                /* Request stream from Downstream client */
                if (!ctx->req_parsed) {
                    if (ngx_dns_parse_packet((const uint8_t *)buf, len, is_tcp, &ctx->req_pkt) == 0) {
                        ctx->req_parse_ok = 1;
                        ctx->req_parsed = 1;
                    }
                }
            }
        }
    }

    return next_filter(s, in, from_upstream);
}

/* Stream Variable Getters */

static ngx_int_t
ngx_stream_dns_var_req_qname(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_stream_dns_ctx_t *ctx = ngx_stream_get_module_ctx(s, ngx_stream_dns_module);
    if (!ctx || !ctx->req_parse_ok || ctx->req_pkt.question.qname[0] == '\0') {
        v->not_found = 1;
        return NGX_OK;
    }

    v->len = ngx_strlen(ctx->req_pkt.question.qname);
    v->data = (u_char *)ctx->req_pkt.question.qname;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    return NGX_OK;
}

static ngx_int_t
ngx_stream_dns_var_req_qtype(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_stream_dns_ctx_t *ctx = ngx_stream_get_module_ctx(s, ngx_stream_dns_module);
    if (!ctx || !ctx->req_parse_ok || ctx->req_pkt.question.qtype_str[0] == '\0') {
        v->not_found = 1;
        return NGX_OK;
    }

    v->len = ngx_strlen(ctx->req_pkt.question.qtype_str);
    v->data = (u_char *)ctx->req_pkt.question.qtype_str;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    return NGX_OK;
}

static ngx_int_t
ngx_stream_dns_var_req_qclass(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_stream_dns_ctx_t *ctx = ngx_stream_get_module_ctx(s, ngx_stream_dns_module);
    if (!ctx || !ctx->req_parse_ok || ctx->req_pkt.question.qclass_str[0] == '\0') {
        v->not_found = 1;
        return NGX_OK;
    }

    v->len = ngx_strlen(ctx->req_pkt.question.qclass_str);
    v->data = (u_char *)ctx->req_pkt.question.qclass_str;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    return NGX_OK;
}

static ngx_int_t
ngx_stream_dns_var_req_id(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_stream_dns_ctx_t *ctx = ngx_stream_get_module_ctx(s, ngx_stream_dns_module);
    if (!ctx || !ctx->req_parse_ok) {
        v->not_found = 1;
        return NGX_OK;
    }

    u_char *p = ngx_pnalloc(s->connection->pool, 16);
    if (p == NULL) return NGX_ERROR;

    v->len = ngx_sprintf(p, "%ui", (ngx_uint_t)ctx->req_pkt.header.id) - p;
    v->data = p;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    return NGX_OK;
}

static ngx_int_t
ngx_stream_dns_var_resp_id(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_stream_dns_ctx_t *ctx = ngx_stream_get_module_ctx(s, ngx_stream_dns_module);
    if (!ctx || !ctx->resp_parse_ok) {
        v->not_found = 1;
        return NGX_OK;
    }

    u_char *p = ngx_pnalloc(s->connection->pool, 16);
    if (p == NULL) return NGX_ERROR;

    v->len = ngx_sprintf(p, "%ui", (ngx_uint_t)ctx->resp_pkt.header.id) - p;
    v->data = p;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    return NGX_OK;
}

static ngx_int_t
ngx_stream_dns_var_resp_rcode(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_stream_dns_ctx_t *ctx = ngx_stream_get_module_ctx(s, ngx_stream_dns_module);
    if (!ctx || !ctx->resp_parse_ok) {
        v->not_found = 1;
        return NGX_OK;
    }

    u_char *p = ngx_pnalloc(s->connection->pool, 16);
    if (p == NULL) return NGX_ERROR;

    v->len = ngx_sprintf(p, "%ui", (ngx_uint_t)ctx->resp_pkt.header.rcode) - p;
    v->data = p;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    return NGX_OK;
}

static ngx_int_t
ngx_stream_dns_var_resp_rcode_str(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_stream_dns_ctx_t *ctx = ngx_stream_get_module_ctx(s, ngx_stream_dns_module);
    if (!ctx || !ctx->resp_parse_ok) {
        v->not_found = 1;
        return NGX_OK;
    }

    const char *str = ngx_dns_rcode_to_str(ctx->resp_pkt.header.rcode);
    v->len = ngx_strlen(str);
    v->data = (u_char *)str;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    return NGX_OK;
}

static ngx_int_t
ngx_stream_dns_var_resp_ancount(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_stream_dns_ctx_t *ctx = ngx_stream_get_module_ctx(s, ngx_stream_dns_module);
    if (!ctx || !ctx->resp_parse_ok) {
        v->not_found = 1;
        return NGX_OK;
    }

    u_char *p = ngx_pnalloc(s->connection->pool, 16);
    if (p == NULL) return NGX_ERROR;

    v->len = ngx_sprintf(p, "%ui", (ngx_uint_t)ctx->resp_pkt.parsed_answers) - p;
    v->data = p;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    return NGX_OK;
}

static ngx_int_t
ngx_stream_dns_var_resp_ips(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_stream_dns_ctx_t *ctx = ngx_stream_get_module_ctx(s, ngx_stream_dns_module);
    if (!ctx || !ctx->resp_parse_ok) {
        v->not_found = 1;
        return NGX_OK;
    }

    char *buf = ngx_pnalloc(s->connection->pool, 512);
    if (buf == NULL) return NGX_ERROR;

    size_t len = ngx_dns_answers_to_ips(&ctx->resp_pkt, buf, 512);
    if (len == 0) {
        v->not_found = 1;
        return NGX_OK;
    }

    v->len = len;
    v->data = (u_char *)buf;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    return NGX_OK;
}

static ngx_int_t
ngx_stream_dns_var_resp_json(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_stream_dns_ctx_t *ctx = ngx_stream_get_module_ctx(s, ngx_stream_dns_module);
    if (!ctx || !ctx->resp_parse_ok) {
        v->not_found = 1;
        return NGX_OK;
    }

    char *buf = ngx_pnalloc(s->connection->pool, 2048);
    if (buf == NULL) return NGX_ERROR;

    size_t len = ngx_dns_packet_to_json(&ctx->resp_pkt, buf, 2048);
    if (len == 0) {
        v->not_found = 1;
        return NGX_OK;
    }

    v->len = len;
    v->data = (u_char *)buf;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    return NGX_OK;
}

/* Table of exposed stream module variables */
static ngx_stream_variable_t ngx_stream_dns_vars[] = {
    { ngx_string("dns_request_name"), NULL, ngx_stream_dns_var_req_qname, 0, 0, 0 },
    { ngx_string("dns_request_qname"), NULL, ngx_stream_dns_var_req_qname, 0, 0, 0 },
    { ngx_string("dns_request_type"), NULL, ngx_stream_dns_var_req_qtype, 0, 0, 0 },
    { ngx_string("dns_request_qtype"), NULL, ngx_stream_dns_var_req_qtype, 0, 0, 0 },
    { ngx_string("dns_request_class"), NULL, ngx_stream_dns_var_req_qclass, 0, 0, 0 },
    { ngx_string("dns_request_qclass"), NULL, ngx_stream_dns_var_req_qclass, 0, 0, 0 },
    { ngx_string("dns_request_id"), NULL, ngx_stream_dns_var_req_id, 0, 0, 0 },
    { ngx_string("dns_response_id"), NULL, ngx_stream_dns_var_resp_id, 0, 0, 0 },
    { ngx_string("dns_response_rcode"), NULL, ngx_stream_dns_var_resp_rcode, 0, 0, 0 },
    { ngx_string("dns_response_rcode_str"), NULL, ngx_stream_dns_var_resp_rcode_str, 0, 0, 0 },
    { ngx_string("dns_response_ancount"), NULL, ngx_stream_dns_var_resp_ancount, 0, 0, 0 },
    { ngx_string("dns_response_ips"), NULL, ngx_stream_dns_var_resp_ips, 0, 0, 0 },
    { ngx_string("dns_response_answers_json"), NULL, ngx_stream_dns_var_resp_json, 0, 0, 0 },

    ngx_stream_null_variable
};

/* Module Command Directives */
static ngx_command_t ngx_stream_dns_commands[] = {

    { ngx_string("dns_parse"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_stream_dns_parse_directive,
      NGX_STREAM_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("dns_parse_max_size"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_dns_srv_conf_t, max_size),
      NULL },

      ngx_null_command
};

static ngx_stream_module_t ngx_stream_dns_module_ctx = {
    ngx_stream_dns_add_variables,   /* preconfiguration */
    ngx_stream_dns_init,            /* postconfiguration */

    NULL,                           /* create main configuration */
    NULL,                           /* init main configuration */

    ngx_stream_dns_create_srv_conf, /* create server configuration */
    ngx_stream_dns_merge_srv_conf   /* merge server configuration */
};

ngx_module_t ngx_stream_dns_module = {
    NGX_MODULE_V1,
    &ngx_stream_dns_module_ctx,     /* module context */
    ngx_stream_dns_commands,       /* module directives */
    NGX_STREAM_MODULE,             /* module type */
    NULL,                           /* init master */
    NULL,                           /* init module */
    NULL,                           /* init process */
    NULL,                           /* init thread */
    NULL,                           /* exit thread */
    NULL,                           /* exit process */
    NULL,                           /* exit master */
    NGX_MODULE_V1_PADDING
};

static void *
ngx_stream_dns_create_srv_conf(ngx_conf_t *cf)
{
    ngx_stream_dns_srv_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_stream_dns_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->enable = NGX_CONF_UNSET;
    conf->max_size = NGX_CONF_UNSET_SIZE;

    return conf;
}

static char *
ngx_stream_dns_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_stream_dns_srv_conf_t *prev = parent;
    ngx_stream_dns_srv_conf_t *conf = child;

    ngx_conf_merge_value(conf->enable, prev->enable, 0);
    ngx_conf_merge_size_value(conf->max_size, prev->max_size, 4096);

    return NGX_CONF_OK;
}

static ngx_int_t
ngx_stream_dns_init(ngx_conf_t *cf)
{
    ngx_stream_core_main_conf_t *cmcf;
    ngx_stream_handler_pt       *h;

    /* 1. Register Preread Handler */
    cmcf = ngx_stream_conf_get_module_main_conf(cf, ngx_stream_core_module);
    if (cmcf == NULL) {
        return NGX_ERROR;
    }

    h = ngx_array_push(&cmcf->phases[NGX_STREAM_PREREAD_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_stream_dns_preread_handler;

    /* 2. Register Stream Top Filter Chain */
    next_filter = ngx_stream_top_filter;
    ngx_stream_top_filter = ngx_stream_dns_filter;

    return NGX_OK;
}

static ngx_int_t
ngx_stream_dns_add_variables(ngx_conf_t *cf)
{
    ngx_stream_variable_t *var, *v;

    for (v = ngx_stream_dns_vars; v->name.len; v++) {
        var = ngx_stream_add_variable(cf, &v->name, v->flags);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = v->get_handler;
        var->data = v->data;
    }

    return NGX_OK;
}

static char *
ngx_stream_dns_parse_directive(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_stream_dns_srv_conf_t *scf = conf;
    ngx_str_t *value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "on") == 0) {
        scf->enable = 1;
        return NGX_CONF_OK;
    }

    if (ngx_strcmp(value[1].data, "off") == 0) {
        scf->enable = 0;
        return NGX_CONF_OK;
    }

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "invalid value \"%V\" in \"%V\" directive, it must be \"on\" or \"off\"",
                       &value[1], &value[0]);
    return NGX_CONF_ERROR;
}
