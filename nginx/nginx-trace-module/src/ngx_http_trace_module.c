/*
 * ngx_http_trace_module - module.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_trace_module.h"

static ngx_command_t  ngx_http_trace_commands[] = {

    { ngx_string("trace"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_trace_loc_conf_t, enable),
      NULL },

    { ngx_string("trace_zone"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE2,
      ngx_http_trace_zone,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("trace_control"),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_trace_control,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    /* --- M1: per-scope capture selection (http/server/location) ----------- */

    { ngx_string("trace_watch"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_trace_set_str_array,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_trace_loc_conf_t, watch),
      NULL },

    { ngx_string("trace_redact"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_trace_set_str_array,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_trace_loc_conf_t, redact),
      NULL },

    { ngx_string("trace_upstream_capture"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_trace_set_upstream_capture,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_trace_loc_conf_t, upstream_capture),
      NULL },

    { ngx_string("trace_body_capture"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_trace_set_body_capture,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_trace_loc_conf_t, body_capture),
      NULL },

    { ngx_string("trace_body_max"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_trace_loc_conf_t, body_max),
      NULL },

    { ngx_string("trace_grpc_proto"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_trace_loc_conf_t, grpc_proto),
      NULL },

    { ngx_string("trace_fault_only"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE12,
      ngx_http_trace_set_fault_only,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    /* --- M1: global caps and layer toggles (main context only) ------------ */

    { ngx_string("trace_max_sessions"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_trace_main_conf_t, max_sessions),
      NULL },

    { ngx_string("trace_max_transactions"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_trace_main_conf_t, max_transactions),
      NULL },

    { ngx_string("trace_retention"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_trace_main_conf_t, retention),
      NULL },

    { ngx_string("trace_intercept"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_trace_main_conf_t, intercept),
      NULL },

    { ngx_string("trace_ebpf"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE12,
      ngx_http_trace_set_ebpf,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

    /*
     * M8.6 — trace_hardened on|off (NFR-SEC-7). A single global kill switch for
     * deployments where payload bytes must never be captured: it force-disables
     * body capture regardless of any per-location trace_body_capture, and
     * suppresses the raw-byte error_log emit. Main-context only, because a
     * security posture that could be relaxed per-location would be worthless.
     */
    { ngx_string("trace_hardened"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_trace_main_conf_t, hardened),
      NULL },

    /* --- M1: self-diagnostics logging (main context) ---------------------- */

    { ngx_string("trace_log"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_trace_main_conf_t, log_path),
      NULL },

    { ngx_string("trace_log_level"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_http_trace_set_log_level,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_trace_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_trace_postconfiguration,      /* postconfiguration */

    ngx_http_trace_create_main_conf,   /* create main configuration */
    ngx_http_trace_init_main_conf,     /* init main configuration */

    ngx_http_trace_create_srv_conf,    /* create server configuration */
    ngx_http_trace_merge_srv_conf,     /* merge server configuration */

    ngx_http_trace_create_loc_conf,    /* create location configuration */
    ngx_http_trace_merge_loc_conf      /* merge location configuration */
};


ngx_module_t  ngx_http_trace_module = {
    NGX_MODULE_V1,
    &ngx_http_trace_module_ctx,        /* module context */
    ngx_http_trace_commands,           /* module directives */
    NGX_HTTP_MODULE,                   /* module type */
    NULL,                              /* init master */
    NULL,                              /* init module */
    NULL,                              /* init process */
    NULL,                              /* init thread */
    NULL,                              /* exit thread */
    NULL,                              /* exit process */
    NULL,                              /* exit master */
    NGX_MODULE_V1_PADDING
};



void *
ngx_http_trace_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_trace_main_conf_t  *mcf;

    mcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_trace_main_conf_t));
    if (mcf == NULL) {
        return NULL;
    }

    /* conf-unset-init: sentinels so init_main_conf can apply real defaults
     * only where the operator did not set an explicit value. shm_zone,
     * log_path and log_file are left NULL/zeroed by pcalloc (NULL => inert). */
    mcf->max_sessions     = NGX_CONF_UNSET_UINT;
    mcf->max_transactions = NGX_CONF_UNSET_UINT;
    mcf->retention        = NGX_CONF_UNSET_MSEC;
    mcf->intercept        = NGX_CONF_UNSET;
    mcf->ebpf             = NGX_CONF_UNSET_UINT;
    mcf->ebpf_tls         = NGX_CONF_UNSET;
    mcf->hardened         = NGX_CONF_UNSET;
    mcf->log_level        = NGX_CONF_UNSET_UINT;

    return mcf;
}

char *
ngx_http_trace_init_main_conf(ngx_conf_t *cf, void *conf)
{
    ngx_http_trace_main_conf_t  *mcf = conf;

    /* Defaults mirror SPEC §4 (FR-CFG-9/10/14 and the layer toggles). */
    ngx_conf_init_uint_value(mcf->max_sessions, 4);
    ngx_conf_init_uint_value(mcf->max_transactions, NGX_HTTP_TRACE_RING_SLOTS);
    ngx_conf_init_msec_value(mcf->retention, 86400000);   /* 24h */
    ngx_conf_init_value(mcf->intercept, 0);               /* Layer 2 off  */
    ngx_conf_init_uint_value(mcf->ebpf, NGX_HTTP_TRACE_EBPF_OFF);
    ngx_conf_init_value(mcf->ebpf_tls, 0);
    ngx_conf_init_value(mcf->hardened, 0);        /* NFR-SEC-7 opt-in       */
    ngx_conf_init_uint_value(mcf->log_level, NGX_HTTP_TRACE_LOG_ERROR);

    /* "off" for the diagnostics path means "route to nginx error_log only":
     * leave log_path empty / log_file NULL. A real path is opened lazily in a
     * later milestone (M1.5 wires the sink; parsing/validation lands here). */
    if (mcf->log_path.len == sizeof("off") - 1
        && ngx_strncmp(mcf->log_path.data, "off", 3) == 0)
    {
        ngx_str_null(&mcf->log_path);
    }

    if (mcf->log_path.len != 0) {
        mcf->log_file = ngx_conf_open_file(cf->cycle, &mcf->log_path);
        if (mcf->log_file == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}

/*
 * Server-scope capture config. It shares the loc_conf layout so the standard
 * http->server->location merge chain carries trace/watch/redact/body/upstream
 * settings down to each location (FR-CFG-*, conf-merge-all-fields).
 */
void *
ngx_http_trace_create_srv_conf(ngx_conf_t *cf)
{
    return ngx_http_trace_create_loc_conf(cf);
}

char *
ngx_http_trace_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
    return ngx_http_trace_merge_loc_conf(cf, parent, child);
}

void *
ngx_http_trace_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_trace_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_trace_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /* conf-unset-init: distinguish "unset" from an explicit value so a more
     * specific scope only overrides when the operator set something there.
     * watch/redact arrays and grpc_proto are left NULL/zero (pcalloc). */
    conf->enable           = NGX_CONF_UNSET;
    conf->upstream_capture = NGX_CONF_UNSET_UINT;
    conf->body_capture     = NGX_CONF_UNSET_UINT;
    conf->body_max         = NGX_CONF_UNSET_SIZE;
    conf->watch            = NGX_CONF_UNSET_PTR;
    conf->redact           = NGX_CONF_UNSET_PTR;
    conf->fault_only       = NGX_CONF_UNSET;
    conf->fault_code       = NGX_CONF_UNSET_UINT;

    return conf;
}

char *
ngx_http_trace_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_trace_loc_conf_t  *prev = parent;
    ngx_http_trace_loc_conf_t  *conf = child;

    /* conf-merge-all-fields: every field merged so inheritance is complete. */
    ngx_conf_merge_value(conf->enable, prev->enable, 0);
    ngx_conf_merge_uint_value(conf->upstream_capture, prev->upstream_capture,
                              NGX_HTTP_TRACE_UP_HEADERS);
    ngx_conf_merge_uint_value(conf->body_capture, prev->body_capture,
                              NGX_HTTP_TRACE_BODY_OFF);
    ngx_conf_merge_size_value(conf->body_max, prev->body_max, 8192);
    ngx_conf_merge_ptr_value(conf->watch, prev->watch, NULL);
    ngx_conf_merge_ptr_value(conf->redact, prev->redact, NULL);
    ngx_conf_merge_str_value(conf->grpc_proto, prev->grpc_proto, "");
    ngx_conf_merge_value(conf->fault_only, prev->fault_only, 0);
    ngx_conf_merge_uint_value(conf->fault_code, prev->fault_code, 0);

    return NGX_CONF_OK;
}

/*
 * trace_upstream_capture off|headers|full (FR-CFG-6). Stored as the UP_* enum
 * at the offset named in the command entry.
 */
char *
ngx_http_trace_set_upstream_capture(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    char       *p = conf;
    ngx_str_t  *value = cf->args->elts;
    ngx_uint_t *field = (ngx_uint_t *) (p + cmd->offset);

    if (*field != NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }

    if (ngx_strcmp(value[1].data, "off") == 0) {
        *field = NGX_HTTP_TRACE_UP_OFF;
    } else if (ngx_strcmp(value[1].data, "headers") == 0) {
        *field = NGX_HTTP_TRACE_UP_HEADERS;
    } else if (ngx_strcmp(value[1].data, "full") == 0) {
        *field = NGX_HTTP_TRACE_UP_FULL;
    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid trace_upstream_capture value \"%V\": "
                           "expected off|headers|full", &value[1]);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

/*
 * trace_body_capture off|request|response|both (FR-CFG-11). Stored as the
 * BODY_* bit flags so request/response capture are controlled independently.
 */
char *
ngx_http_trace_set_body_capture(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char       *p = conf;
    ngx_str_t  *value = cf->args->elts;
    ngx_uint_t *field = (ngx_uint_t *) (p + cmd->offset);

    if (*field != NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }

    if (ngx_strcmp(value[1].data, "off") == 0) {
        *field = NGX_HTTP_TRACE_BODY_OFF;
    } else if (ngx_strcmp(value[1].data, "request") == 0) {
        *field = NGX_HTTP_TRACE_BODY_REQUEST;
    } else if (ngx_strcmp(value[1].data, "response") == 0) {
        *field = NGX_HTTP_TRACE_BODY_RESPONSE;
    } else if (ngx_strcmp(value[1].data, "both") == 0) {
        *field = NGX_HTTP_TRACE_BODY_BOTH;
    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid trace_body_capture value \"%V\": "
                           "expected off|request|response|both", &value[1]);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

/*
 * trace_ebpf off|on [tls] (FR-CFG-8). First arg toggles the add-on; the
 * optional second arg "tls" enables plaintext capture. Behavior is wired in
 * Phase 4; here we only parse and store into the main conf.
 */
char *
ngx_http_trace_set_ebpf(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_trace_main_conf_t  *mcf = conf;
    ngx_str_t                   *value = cf->args->elts;

    if (mcf->ebpf != NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }

    if (ngx_strcmp(value[1].data, "off") == 0) {
        mcf->ebpf = NGX_HTTP_TRACE_EBPF_OFF;
    } else if (ngx_strcmp(value[1].data, "on") == 0) {
        mcf->ebpf = NGX_HTTP_TRACE_EBPF_ON;
    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid trace_ebpf value \"%V\": expected off|on",
                           &value[1]);
        return NGX_CONF_ERROR;
    }

    if (cf->args->nelts == 3) {
        if (ngx_strcmp(value[2].data, "tls") != 0) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid trace_ebpf option \"%V\": "
                               "expected tls", &value[2]);
            return NGX_CONF_ERROR;
        }
        mcf->ebpf_tls = 1;
    }

    return NGX_CONF_OK;
}

/*
 * trace_log_level off|error|warn|info|debug|trace (FR-CFG-16). Maps the name
 * to the numeric ladder so runtime emission is a single comparison.
 */
char *
ngx_http_trace_set_log_level(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_trace_main_conf_t  *mcf = conf;
    ngx_str_t                   *value = cf->args->elts;

    static const struct {
        ngx_str_t   name;
        ngx_uint_t  level;
    } levels[] = {
        { ngx_string("off"),   NGX_HTTP_TRACE_LOG_OFF   },
        { ngx_string("error"), NGX_HTTP_TRACE_LOG_ERROR },
        { ngx_string("warn"),  NGX_HTTP_TRACE_LOG_WARN  },
        { ngx_string("info"),  NGX_HTTP_TRACE_LOG_INFO  },
        { ngx_string("debug"), NGX_HTTP_TRACE_LOG_DEBUG },
        { ngx_string("trace"), NGX_HTTP_TRACE_LOG_TRACE },
        { ngx_null_string,     0                        }
    };
    ngx_uint_t  i;

    if (mcf->log_level != NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }

    for (i = 0; levels[i].name.len != 0; i++) {
        if (value[1].len == levels[i].name.len
            && ngx_strncmp(value[1].data, levels[i].name.data,
                           value[1].len) == 0)
        {
            mcf->log_level = levels[i].level;
            return NGX_CONF_OK;
        }
    }

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "invalid trace_log_level \"%V\": expected "
                       "off|error|warn|info|debug|trace", &value[1]);
    return NGX_CONF_ERROR;
}

/*
 * Leveled self-diagnostics emit (FR-LOG-1..6). Short-circuits before any
 * formatting when the requested level exceeds the configured threshold, so
 * leaving diagnostics configured-but-quiet costs effectively nothing
 * (NFR-LOG-1). A dedicated `trace_log` sink receives a plain timestamped line;
 * otherwise the message is routed to nginx's error_log at an equivalent
 * severity. Callers MUST NOT pass payload bytes or secrets (FR-LOG-6).
 */
void
ngx_http_trace_diag(ngx_http_trace_main_conf_t *mcf, ngx_uint_t level,
    ngx_log_t *log, const char *fmt, ...)
{
    va_list   args;
    u_char    line[512], *p;
    ngx_uint_t  err_level;

    if (mcf == NULL || level == NGX_HTTP_TRACE_LOG_OFF
        || mcf->log_level < level)
    {
        return;                         /* below threshold: no work at all */
    }

    if (mcf->log_file != NULL) {
        /* Dedicated sink: "<time> [trace] <message>\n". */
        p = ngx_cpymem(line, ngx_cached_err_log_time.data,
                       ngx_cached_err_log_time.len);
        p = ngx_cpymem(p, " [trace] ", sizeof(" [trace] ") - 1);

        va_start(args, fmt);
        p = ngx_vslprintf(p, line + sizeof(line) - 1, fmt, args);
        va_end(args);

        *p++ = '\n';

        (void) ngx_write_fd(mcf->log_file->fd, line, p - line);
        return;
    }

    if (log == NULL) {
        return;
    }

    /* Map our ladder onto nginx severities for the error_log fallback. */
    switch (level) {
    case NGX_HTTP_TRACE_LOG_ERROR:  err_level = NGX_LOG_ERR;    break;
    case NGX_HTTP_TRACE_LOG_WARN:   err_level = NGX_LOG_WARN;   break;
    case NGX_HTTP_TRACE_LOG_INFO:   err_level = NGX_LOG_INFO;   break;
    default:                        err_level = NGX_LOG_DEBUG;  break;
    }

    va_start(args, fmt);
    p = ngx_vslprintf(line, line + sizeof(line) - 1, fmt, args);
    va_end(args);
    *p = '\0';

    ngx_log_error(err_level, log, 0, "[trace] %s", line);
}

/*
 * Generic 1+ string-list parser for trace_watch / trace_redact (FR-CFG-3/13).
 * Appends each argument as an ngx_str_t into the array at cmd->offset. The
 * array is lazily created; NGX_CONF_UNSET_PTR marks "not set in this scope".
 */
char *
ngx_http_trace_set_str_array(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char        *p = conf;
    ngx_str_t   *value = cf->args->elts;
    ngx_array_t **a = (ngx_array_t **) (p + cmd->offset);
    ngx_str_t   *item;
    ngx_uint_t   i;

    if (*a == NGX_CONF_UNSET_PTR || *a == NULL) {
        *a = ngx_array_create(cf->pool, cf->args->nelts - 1,
                              sizeof(ngx_str_t));
        if (*a == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    for (i = 1; i < cf->args->nelts; i++) {
        item = ngx_array_push(*a);
        if (item == NULL) {
            return NGX_CONF_ERROR;
        }
        *item = value[i];
    }

    return NGX_CONF_OK;
}

/*
 * trace_fault_only on|off [<code>] (FR-SEL-4). When on, a traced request is
 * recorded provisionally in the request-pool ctx but committed at LOG only if
 * it finalized as a fault. An optional numeric HTTP code narrows the filter to
 * that exact finalizing status (e.g. `trace_fault_only on 502`); 0 means "any
 * fault". Stores the flag at `enable`-style offsets on the loc conf.
 */
char *
ngx_http_trace_set_fault_only(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_trace_loc_conf_t  *tlcf = conf;
    ngx_str_t                  *value = cf->args->elts;
    ngx_int_t                   code;

    if (tlcf->fault_only != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    if (ngx_strcmp(value[1].data, "on") == 0) {
        tlcf->fault_only = 1;
    } else if (ngx_strcmp(value[1].data, "off") == 0) {
        tlcf->fault_only = 0;
    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid trace_fault_only value \"%V\": "
                           "expected on|off", &value[1]);
        return NGX_CONF_ERROR;
    }

    if (cf->args->nelts == 3) {
        code = ngx_atoi(value[2].data, value[2].len);
        if (code < 100 || code > 599) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid trace_fault_only code \"%V\": "
                               "expected an HTTP status 100..599", &value[2]);
            return NGX_CONF_ERROR;
        }
        tlcf->fault_code = (ngx_uint_t) code;
    }

    return NGX_CONF_OK;
}

/*
 * Register a pass-through observer handler in every phase that accepts one.
 * Must run in postconfiguration — the only point where the phases array is
 * still being built (skill:handler-phase-registration).
 *
 * Excluded (internal, reject handlers): NGX_HTTP_FIND_CONFIG_PHASE,
 * NGX_HTTP_POST_REWRITE_PHASE, NGX_HTTP_POST_ACCESS_PHASE.
 */
ngx_int_t
ngx_http_trace_postconfiguration(ngx_conf_t *cf)
{
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_trace_main_conf_t *mcf;
    ngx_http_handler_pt        *h;
    ngx_uint_t                  i;

    /*
     * Each registrable phase gets a dedicated observer so the Layer-1 timeline
     * labels the phase correctly (M2.3). POST_READ gets the selector, which
     * runs first and builds the per-request ctx (M2.2). FIND_CONFIG,
     * POST_REWRITE and POST_ACCESS are internal and reject handlers — their
     * effects are inferred (M2.4).
     */
    static const struct {
        ngx_uint_t           phase;
        ngx_http_handler_pt  handler;
    } observers[] = {
        { NGX_HTTP_POST_READ_PHASE,       ngx_http_trace_post_read_handler   },
        { NGX_HTTP_SERVER_REWRITE_PHASE,  ngx_http_trace_obs_server_rewrite  },
        { NGX_HTTP_REWRITE_PHASE,         ngx_http_trace_obs_rewrite         },
        { NGX_HTTP_PREACCESS_PHASE,       ngx_http_trace_obs_preaccess       },
        { NGX_HTTP_ACCESS_PHASE,          ngx_http_trace_obs_access          }
    };

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    /*
     * M7.4 — Layer-2 version gate. Interception depends on the internal
     * layout of the request/upstream structures, so it is only enabled on
     * nginx releases we have validated. On anything else we warn once and
     * silently degrade to Layer 1 (phase-only naming), which is always safe.
     */
    mcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_trace_module);

    if (mcf != NULL && mcf->intercept == 1) {
        if (nginx_version >= NGX_HTTP_TRACE_L2_MIN_VERSION
            && nginx_version <= NGX_HTTP_TRACE_L2_MAX_VERSION)
        {
            mcf->intercept_active = 1;

            ngx_conf_log_error(NGX_LOG_NOTICE, cf, 0,
                               "[trace] layer2 intercept enabled (nginx %ui)",
                               (ngx_uint_t) nginx_version);

        } else {
            mcf->intercept_active = 0;

            ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                               "[trace] layer2 intercept unsupported on "
                               "nginx %ui (supported %ui..%ui); "
                               "degrading to layer1",
                               (ngx_uint_t) nginx_version,
                               (ngx_uint_t) NGX_HTTP_TRACE_L2_MIN_VERSION,
                               (ngx_uint_t) NGX_HTTP_TRACE_L2_MAX_VERSION);
        }
    }

    for (i = 0; i < sizeof(observers) / sizeof(observers[0]); i++) {
        h = ngx_array_push(&cmcf->phases[observers[i].phase].handlers);
        if (h == NULL) {
            return NGX_ERROR;
        }
        *h = observers[i].handler;
    }

    /* PRECONTENT installs the content-handler trampoline (upstream capture). */
    h = ngx_array_push(&cmcf->phases[NGX_HTTP_PRECONTENT_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_trace_precontent_handler;

    /* LOG phase uses a dedicated handler that returns NGX_OK. */
    h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_trace_log_handler;

    /*
     * M8.2 — install the output filters LAST in postconfiguration, which puts
     * us at the HEAD of the chain (skill:filter-registration-order). That is
     * deliberate: at the head we observe the response body as the client will
     * actually receive it, after gzip/ssi/sub have transformed it, so the
     * preview matches the wire bytes rather than some intermediate form.
     */
    ngx_http_trace_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_trace_header_filter;

    ngx_http_trace_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_trace_body_filter;

    return NGX_OK;
}
