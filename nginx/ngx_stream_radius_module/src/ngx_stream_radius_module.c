/*
 * ngx_stream_radius_module.c
 *
 * NGINX Stream RADIUS Protocol Parser Module
 * Supports RFC 2865 (RADIUS Authentication) and RFC 2866 (RADIUS Accounting)
 * Compatible with NGINX Open Source and NGINX Plus
 *
 * Copyright (C) 2024 - nginx-radius-module contributors
 * License: BSD 2-Clause
 */

#include "ngx_stream_radius_module.h"
#include "ngx_stream_radius_parser.h"
#include "ngx_stream_radius_dict.h"
#include "ngx_stream_radius_vars.h"


/* -------------------------------------------------------------------------
 * Forward declarations
 * ---------------------------------------------------------------------- */

static ngx_int_t ngx_stream_radius_preread_handler(ngx_stream_session_t *s);
static ngx_int_t ngx_stream_radius_add_variables(ngx_conf_t *cf);

static void     *ngx_stream_radius_create_main_conf(ngx_conf_t *cf);
static void     *ngx_stream_radius_create_srv_conf(ngx_conf_t *cf);
static char     *ngx_stream_radius_merge_srv_conf(ngx_conf_t *cf,
                    void *parent, void *child);

static char     *ngx_stream_radius_dict_directive(ngx_conf_t *cf,
                    ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_stream_radius_init(ngx_conf_t *cf);

static ngx_int_t ngx_stream_radius_variable(ngx_stream_session_t *s,
                    ngx_stream_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_stream_radius_attr_variable(ngx_stream_session_t *s,
                    ngx_stream_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_stream_radius_resp_variable(ngx_stream_session_t *s,
                    ngx_stream_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_stream_radius_resp_attr_variable(
                    ngx_stream_session_t *s,
                    ngx_stream_variable_value_t *v, uintptr_t data);
static ngx_chain_t *ngx_stream_radius_send_chain(ngx_connection_t *c,
                    ngx_chain_t *in, off_t limit);
/* ngx_send_chain_pt = ngx_chain_t *(*)(ngx_connection_t*, ngx_chain_t*, off_t) */


/* -------------------------------------------------------------------------
 * ngx_stream_radius_vsa_var_name
 *
 * Build "radius_vsa_<vendor>_<attr>" with all chars lowercased and
 * hyphens converted to underscores.  Returns number of bytes written
 * (without NUL), or 0 on overflow.
 * ---------------------------------------------------------------------- */

size_t
ngx_stream_radius_vsa_var_name(u_char *buf, size_t buf_len,
    const ngx_str_t *vendor_name, const ngx_str_t *attr_name)
{
    static const u_char prefix[] = "radius_vsa_";
    size_t               i, out = 0;
    u_char               c;

    if (buf_len < sizeof(prefix)) {
        return 0;
    }

    for (i = 0; i < sizeof(prefix) - 1; i++) {
        buf[out++] = prefix[i];
    }

    for (i = 0; i < vendor_name->len && out < buf_len - 1; i++) {
        c = vendor_name->data[i];
        buf[out++] = (c == '-') ? '_' : ngx_tolower(c);
    }

    if (out < buf_len - 1) {
        buf[out++] = '_';
    }

    for (i = 0; i < attr_name->len && out < buf_len - 1; i++) {
        c = attr_name->data[i];
        buf[out++] = (c == '-') ? '_' : ngx_tolower(c);
    }

    buf[out] = '\0';
    return out;
}


/* -------------------------------------------------------------------------
 * Built-in static variable table (packet header + standard RFC attrs)
 * ---------------------------------------------------------------------- */

static ngx_stream_variable_t  ngx_stream_radius_vars[] = {

    /* Packet-level */
    { ngx_string("radius_code"),
      NULL, ngx_stream_radius_variable,
      NGX_RADIUS_VAR_CODE, 0, 0 },

    { ngx_string("radius_code_name"),
      NULL, ngx_stream_radius_variable,
      NGX_RADIUS_VAR_CODE_NAME, 0, 0 },

    { ngx_string("radius_identifier"),
      NULL, ngx_stream_radius_variable,
      NGX_RADIUS_VAR_IDENTIFIER, 0, 0 },

    { ngx_string("radius_length"),
      NULL, ngx_stream_radius_variable,
      NGX_RADIUS_VAR_LENGTH, 0, 0 },

    { ngx_string("radius_authenticator"),
      NULL, ngx_stream_radius_variable,
      NGX_RADIUS_VAR_AUTHENTICATOR, 0, 0 },

    { ngx_string("radius_valid"),
      NULL, ngx_stream_radius_variable,
      NGX_RADIUS_VAR_VALID, 0, 0 },

    /* Upstream response (populated after upstream reply is forwarded) */
    { ngx_string("radius_response_code"),
      NULL, ngx_stream_radius_resp_variable,
      NGX_RADIUS_VAR_RESP_CODE, 0, 0 },

    { ngx_string("radius_response_code_name"),
      NULL, ngx_stream_radius_resp_variable,
      NGX_RADIUS_VAR_RESP_NAME, 0, 0 },

    /* Response packet attributes ($radius_resp_<attrname>)            */
    /* These mirror the request variables but source from the upstream  */
    /* Access-Accept / Access-Reject / Accounting-Response packet.      */
    { ngx_string("radius_resp_user_name"),
      NULL, ngx_stream_radius_resp_attr_variable, 1, 0, 0 },
    { ngx_string("radius_resp_reply_message"),
      NULL, ngx_stream_radius_resp_attr_variable, 18, 0, 0 },
    { ngx_string("radius_resp_framed_ip_address"),
      NULL, ngx_stream_radius_resp_attr_variable, 8, 0, 0 },
    { ngx_string("radius_resp_framed_ip_netmask"),
      NULL, ngx_stream_radius_resp_attr_variable, 9, 0, 0 },
    { ngx_string("radius_resp_framed_protocol"),
      NULL, ngx_stream_radius_resp_attr_variable, 7, 0, 0 },
    { ngx_string("radius_resp_framed_routing"),
      NULL, ngx_stream_radius_resp_attr_variable, 10, 0, 0 },
    { ngx_string("radius_resp_filter_id"),
      NULL, ngx_stream_radius_resp_attr_variable, 11, 0, 0 },
    { ngx_string("radius_resp_framed_mtu"),
      NULL, ngx_stream_radius_resp_attr_variable, 12, 0, 0 },
    { ngx_string("radius_resp_framed_compression"),
      NULL, ngx_stream_radius_resp_attr_variable, 13, 0, 0 },
    { ngx_string("radius_resp_service_type"),
      NULL, ngx_stream_radius_resp_attr_variable, 6, 0, 0 },
    { ngx_string("radius_resp_session_timeout"),
      NULL, ngx_stream_radius_resp_attr_variable, 27, 0, 0 },
    { ngx_string("radius_resp_idle_timeout"),
      NULL, ngx_stream_radius_resp_attr_variable, 28, 0, 0 },
    { ngx_string("radius_resp_termination_action"),
      NULL, ngx_stream_radius_resp_attr_variable, 29, 0, 0 },
    { ngx_string("radius_resp_called_station_id"),
      NULL, ngx_stream_radius_resp_attr_variable, 30, 0, 0 },
    { ngx_string("radius_resp_calling_station_id"),
      NULL, ngx_stream_radius_resp_attr_variable, 31, 0, 0 },
    { ngx_string("radius_resp_nas_identifier"),
      NULL, ngx_stream_radius_resp_attr_variable, 32, 0, 0 },
    { ngx_string("radius_resp_state"),
      NULL, ngx_stream_radius_resp_attr_variable, 24, 0, 0 },
    { ngx_string("radius_resp_class"),
      NULL, ngx_stream_radius_resp_attr_variable, 25, 0, 0 },
    { ngx_string("radius_resp_acct_interim_interval"),
      NULL, ngx_stream_radius_resp_attr_variable, 85, 0, 0 },
    { ngx_string("radius_resp_msg_authenticator"),
      NULL, ngx_stream_radius_resp_attr_variable, 80, 0, 0 },
    { ngx_string("radius_resp_framed_pool"),
      NULL, ngx_stream_radius_resp_attr_variable, 88, 0, 0 },
    { ngx_string("radius_resp_eap_message"),
      NULL, ngx_stream_radius_resp_attr_variable, 79, 0, 0 },
    { ngx_string("radius_resp_nas_ipv6_address"),
      NULL, ngx_stream_radius_resp_attr_variable, 95, 0, 0 },
    { ngx_string("radius_resp_framed_ipv6_prefix"),
      NULL, ngx_stream_radius_resp_attr_variable, 97, 0, 0 },
    { ngx_string("radius_resp_framed_ipv6_pool"),
      NULL, ngx_stream_radius_resp_attr_variable, 100, 0, 0 },
    { ngx_string("radius_resp_connect_info"),
      NULL, ngx_stream_radius_resp_attr_variable, 77, 0, 0 },
    { ngx_string("radius_resp_tunnel_type"),
      NULL, ngx_stream_radius_resp_attr_variable, 64, 0, 0 },
    { ngx_string("radius_resp_tunnel_medium_type"),
      NULL, ngx_stream_radius_resp_attr_variable, 65, 0, 0 },
    { ngx_string("radius_resp_tunnel_private_group_id"),
      NULL, ngx_stream_radius_resp_attr_variable, 81, 0, 0 },

    /* RFC 2865 - Authentication */
    { ngx_string("radius_user_name"),
      NULL, ngx_stream_radius_attr_variable, 1, 0, 0 },

    { ngx_string("radius_user_password"),
      NULL, ngx_stream_radius_attr_variable, 2, 0, 0 },

    { ngx_string("radius_chap_password"),
      NULL, ngx_stream_radius_attr_variable, 3, 0, 0 },

    { ngx_string("radius_nas_ip_address"),
      NULL, ngx_stream_radius_attr_variable, 4, 0, 0 },

    { ngx_string("radius_nas_port"),
      NULL, ngx_stream_radius_attr_variable, 5, 0, 0 },

    { ngx_string("radius_service_type"),
      NULL, ngx_stream_radius_attr_variable, 6, 0, 0 },

    { ngx_string("radius_framed_protocol"),
      NULL, ngx_stream_radius_attr_variable, 7, 0, 0 },

    { ngx_string("radius_framed_ip_address"),
      NULL, ngx_stream_radius_attr_variable, 8, 0, 0 },

    { ngx_string("radius_framed_ip_netmask"),
      NULL, ngx_stream_radius_attr_variable, 9, 0, 0 },

    { ngx_string("radius_filter_id"),
      NULL, ngx_stream_radius_attr_variable, 11, 0, 0 },

    { ngx_string("radius_framed_mtu"),
      NULL, ngx_stream_radius_attr_variable, 12, 0, 0 },

    { ngx_string("radius_reply_message"),
      NULL, ngx_stream_radius_attr_variable, 18, 0, 0 },

    { ngx_string("radius_state"),
      NULL, ngx_stream_radius_attr_variable, 24, 0, 0 },

    { ngx_string("radius_class"),
      NULL, ngx_stream_radius_attr_variable, 25, 0, 0 },

    { ngx_string("radius_session_timeout"),
      NULL, ngx_stream_radius_attr_variable, 27, 0, 0 },

    { ngx_string("radius_idle_timeout"),
      NULL, ngx_stream_radius_attr_variable, 28, 0, 0 },

    { ngx_string("radius_called_station_id"),
      NULL, ngx_stream_radius_attr_variable, 30, 0, 0 },

    { ngx_string("radius_calling_station_id"),
      NULL, ngx_stream_radius_attr_variable, 31, 0, 0 },

    { ngx_string("radius_nas_identifier"),
      NULL, ngx_stream_radius_attr_variable, 32, 0, 0 },

    { ngx_string("radius_proxy_state"),
      NULL, ngx_stream_radius_attr_variable, 33, 0, 0 },

    /* RFC 2866 - Accounting */
    { ngx_string("radius_acct_status_type"),
      NULL, ngx_stream_radius_attr_variable, 40, 0, 0 },

    { ngx_string("radius_acct_delay_time"),
      NULL, ngx_stream_radius_attr_variable, 41, 0, 0 },

    { ngx_string("radius_acct_input_octets"),
      NULL, ngx_stream_radius_attr_variable, 42, 0, 0 },

    { ngx_string("radius_acct_output_octets"),
      NULL, ngx_stream_radius_attr_variable, 43, 0, 0 },

    { ngx_string("radius_acct_session_id"),
      NULL, ngx_stream_radius_attr_variable, 44, 0, 0 },

    { ngx_string("radius_acct_session_time"),
      NULL, ngx_stream_radius_attr_variable, 46, 0, 0 },

    { ngx_string("radius_acct_input_packets"),
      NULL, ngx_stream_radius_attr_variable, 47, 0, 0 },

    { ngx_string("radius_acct_output_packets"),
      NULL, ngx_stream_radius_attr_variable, 48, 0, 0 },

    { ngx_string("radius_acct_terminate_cause"),
      NULL, ngx_stream_radius_attr_variable, 49, 0, 0 },

    /* RFC 2869 - Extensions */
    { ngx_string("radius_acct_input_gigawords"),
      NULL, ngx_stream_radius_attr_variable, 52, 0, 0 },

    { ngx_string("radius_acct_output_gigawords"),
      NULL, ngx_stream_radius_attr_variable, 53, 0, 0 },

    { ngx_string("radius_event_timestamp"),
      NULL, ngx_stream_radius_attr_variable, 55, 0, 0 },

    { ngx_string("radius_chap_challenge"),
      NULL, ngx_stream_radius_attr_variable, 60, 0, 0 },

    { ngx_string("radius_nas_port_type"),
      NULL, ngx_stream_radius_attr_variable, 61, 0, 0 },

    { ngx_string("radius_connect_info"),
      NULL, ngx_stream_radius_attr_variable, 77, 0, 0 },

    { ngx_string("radius_eap_message"),
      NULL, ngx_stream_radius_attr_variable, 79, 0, 0 },

    { ngx_string("radius_msg_authenticator"),
      NULL, ngx_stream_radius_attr_variable, 80, 0, 0 },

    { ngx_string("radius_acct_interim_interval"),
      NULL, ngx_stream_radius_attr_variable, 85, 0, 0 },

    { ngx_string("radius_nas_port_id"),
      NULL, ngx_stream_radius_attr_variable, 87, 0, 0 },

    { ngx_string("radius_framed_pool"),
      NULL, ngx_stream_radius_attr_variable, 88, 0, 0 },

    /* RFC 3162 - IPv6 */
    { ngx_string("radius_nas_ipv6_address"),
      NULL, ngx_stream_radius_attr_variable, 95, 0, 0 },

    { ngx_string("radius_framed_ipv6_prefix"),
      NULL, ngx_stream_radius_attr_variable, 97, 0, 0 },

    { ngx_string("radius_framed_ipv6_pool"),
      NULL, ngx_stream_radius_attr_variable, 100, 0, 0 },

    ngx_stream_null_variable
};


/* -------------------------------------------------------------------------
 * Module directives
 * ---------------------------------------------------------------------- */

static ngx_command_t  ngx_stream_radius_commands[] = {

    /*
     * radius_parse on|off;
     *   Enable RADIUS packet parsing for this server block.
     */
    { ngx_string("radius_parse"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_radius_srv_conf_t, enabled),
      NULL },

    /*
     * radius_dict /path/to/vendor.dict;
     *
     *   Load a FreeRADIUS-compatible vendor dictionary file.
     *   May be specified multiple times.
     *
     *   IMPORTANT: dictionary files are loaded and VSA variables are
     *   registered at configuration-parse time (right when this directive
     *   is encountered), so they are available in subsequent log_format,
     *   map, and other directives that reference $radius_vsa_* variables.
     *
     *   VSA variable names follow the pattern:
     *     $radius_vsa_<vendorname>_<attrname>
     *   where vendorname and attrname are lower-cased with hyphens
     *   converted to underscores, e.g.:
     *     Cisco / Cisco-AVPair   -> $radius_vsa_cisco_cisco_avpair
     *     Mikrotik / Rate-Limit  -> $radius_vsa_mikrotik_rate_limit
     */
    { ngx_string("radius_dict"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_stream_radius_dict_directive,
      NGX_STREAM_MAIN_CONF_OFFSET,
      0,
      NULL },

    /*
     * radius_secret <shared-secret>;
     */
    { ngx_string("radius_secret"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_radius_srv_conf_t, secret),
      NULL },

    /*
     * radius_buffer_size <size>;
     *   Maximum size of a RADIUS packet to buffer (default: 4096).
     */
    { ngx_string("radius_buffer_size"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_STREAM_SRV_CONF_OFFSET,
      offsetof(ngx_stream_radius_srv_conf_t, buffer_size),
      NULL },

    ngx_null_command
};


/* -------------------------------------------------------------------------
 * Module context
 * ---------------------------------------------------------------------- */

static ngx_stream_module_t  ngx_stream_radius_module_ctx = {
    ngx_stream_radius_add_variables,    /* preconfiguration  */
    ngx_stream_radius_init,             /* postconfiguration */

    ngx_stream_radius_create_main_conf, /* create main conf  */
    NULL,                               /* init main conf    */

    ngx_stream_radius_create_srv_conf,  /* create srv conf   */
    ngx_stream_radius_merge_srv_conf    /* merge srv conf    */
};


/* -------------------------------------------------------------------------
 * Module definition
 * ---------------------------------------------------------------------- */

ngx_module_t  ngx_stream_radius_module = {
    NGX_MODULE_V1,
    &ngx_stream_radius_module_ctx,
    ngx_stream_radius_commands,
    NGX_STREAM_MODULE,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NGX_MODULE_V1_PADDING
};


/* =========================================================================
 * Configuration callbacks
 * ====================================================================== */

static void *
ngx_stream_radius_create_main_conf(ngx_conf_t *cf)
{
    ngx_stream_radius_main_conf_t *mcf;

    mcf = ngx_pcalloc(cf->pool, sizeof(ngx_stream_radius_main_conf_t));
    if (mcf == NULL) {
        return NULL;
    }
    /* dict is NULL until the first radius_dict directive is processed */
    return mcf;
}


static void *
ngx_stream_radius_create_srv_conf(ngx_conf_t *cf)
{
    ngx_stream_radius_srv_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_stream_radius_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->enabled     = NGX_CONF_UNSET;
    conf->buffer_size = NGX_CONF_UNSET_SIZE;

    return conf;
}


static char *
ngx_stream_radius_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_stream_radius_srv_conf_t *prev = parent;
    ngx_stream_radius_srv_conf_t *conf = child;

    ngx_conf_merge_value(conf->enabled, prev->enabled, 0);
    ngx_conf_merge_size_value(conf->buffer_size, prev->buffer_size,
                              NGX_RADIUS_DEFAULT_BUFFER_SIZE);
    ngx_conf_merge_str_value(conf->secret, prev->secret, "");
    ngx_conf_merge_str_value(conf->var_prefix, prev->var_prefix,
                             NGX_RADIUS_DEFAULT_VAR_PREFIX);
    ngx_conf_merge_str_value(conf->vsa_prefix, prev->vsa_prefix,
                             NGX_RADIUS_DEFAULT_VSA_PREFIX);

    if (conf->buffer_size > NGX_RADIUS_MAX_PACKET_LEN) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "radius_buffer_size exceeds RADIUS maximum (%d)",
                           NGX_RADIUS_MAX_PACKET_LEN);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


/* -------------------------------------------------------------------------
 * radius_dict directive handler
 *
 * Called at config-parse time.  We load the dictionary file immediately
 * and register every VSA variable via ngx_stream_add_variable() right
 * here, so that subsequent log_format / map / if directives can reference
 * $radius_vsa_* variables without getting "[emerg] unknown variable".
 * ---------------------------------------------------------------------- */

static char *
ngx_stream_radius_dict_directive(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    ngx_stream_radius_main_conf_t *mcf = conf;
    ngx_str_t                     *value;
    ngx_uint_t                     vendor_count_before;
    ngx_radius_vendor_def_t       *vendors;
    ngx_radius_attr_def_t         *vattrs;
    ngx_uint_t                     i, j;
    u_char                         vname_buf[256];
    ngx_str_t                      vname;
    ngx_stream_variable_t         *var;

    value = cf->args->elts;   /* value[1] = path */

    /* Create the dictionary on first use */
    if (mcf->dict == NULL) {
        mcf->dict = ngx_stream_radius_dict_create(cf->pool);
        if (mcf->dict == NULL) {
            return NGX_CONF_ERROR;
        }

        if (ngx_stream_radius_dict_load_builtins(mcf->dict) != NGX_OK) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "radius: failed to load built-in dictionary");
            return NGX_CONF_ERROR;
        }
    }

    /* Record how many vendors exist before loading so we only register
     * variables for newly added VSAs from this file.                    */
    vendor_count_before = mcf->dict->vendors.nelts;

    ngx_conf_log_error(NGX_LOG_NOTICE, cf, 0,
                       "radius: loading dictionary \"%V\"", &value[1]);

    if (ngx_stream_radius_dict_load_file(mcf->dict, &value[1],
                                         cf->log) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "radius: failed to load dictionary \"%V\"",
                           &value[1]);
        return NGX_CONF_ERROR;
    }

    /*
     * Register nginx variables for every VSA in vendors added by this file.
     *
     * Variable name: radius_vsa_<vendorname_lower>_<attrname_lower_underscore>
     *
     * All hyphens are converted to underscores and all characters are
     * lowercased so that nginx's case-insensitive variable lookup can find
     * them (nginx normalises variable names to lowercase internally).
     *
     * The uintptr_t 'data' field encodes:
     *   bits 63..8  = vendor_id  (32 significant bits)
     *   bits  7..0  = attr_type  (vendor sub-attribute code)
     * See NGX_RADIUS_VSA_DATA / NGX_RADIUS_VSA_VENDOR / NGX_RADIUS_VSA_ATYPE.
     */
    vendors = mcf->dict->vendors.elts;

    for (i = vendor_count_before; i < mcf->dict->vendors.nelts; i++) {
        ngx_radius_vendor_def_t *v = &vendors[i];

        vattrs = v->attrs.elts;
        for (j = 0; j < v->attrs.nelts; j++) {
            ngx_radius_attr_def_t *a = &vattrs[j];

            vname.len = ngx_stream_radius_vsa_var_name(
                            vname_buf, sizeof(vname_buf),
                            &v->name, &a->name);
            if (vname.len == 0) {
                ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                    "radius: VSA variable name too long for vendor %V attr %V"
                    " — skipping", &v->name, &a->name);
                continue;
            }
            vname.data = vname_buf;

            var = ngx_stream_add_variable(cf, &vname,
                                          NGX_STREAM_VAR_NOCACHEABLE);
            if (var == NULL) {
                return NGX_CONF_ERROR;
            }

            /* Only set handler if not already registered by a previous dict */
            if (var->get_handler == NULL) {
                var->get_handler = ngx_stream_radius_var_vsa;
                var->data        = NGX_RADIUS_VSA_DATA(v->id, a->type_code);
            }

            ngx_conf_log_error(NGX_LOG_DEBUG, cf, 0,
                "radius: registered $%V (vendor=%ui type=%ui)",
                &vname, v->id, a->type_code);
        }
    }

    return NGX_CONF_OK;
}


/* =========================================================================
 * Postconfiguration: initialise dict from main_conf, install preread handler
 * ====================================================================== */

static ngx_int_t
ngx_stream_radius_init(ngx_conf_t *cf)
{
    ngx_stream_handler_pt          *h;
    ngx_stream_core_main_conf_t    *cmcf;
    ngx_stream_radius_main_conf_t  *mcf;
    ngx_stream_radius_srv_conf_t   *rcf;
    ngx_stream_core_srv_conf_t    **cscf;
    ngx_uint_t                      i;

    cmcf = ngx_stream_conf_get_module_main_conf(cf, ngx_stream_core_module);
    mcf  = ngx_stream_conf_get_module_main_conf(cf, ngx_stream_radius_module);

    /*
     * If no radius_dict was declared, create the dict now with RFC 2865/2866
     * built-ins only so standard-attribute variables still decode correctly.
     */
    if (mcf->dict == NULL) {
        mcf->dict = ngx_stream_radius_dict_create(cf->pool);
        if (mcf->dict == NULL) {
            return NGX_ERROR;
        }
        if (ngx_stream_radius_dict_load_builtins(mcf->dict) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    /*
     * Propagate the shared dict pointer to every server-block srv_conf.
     *
     * merge_srv_conf runs BEFORE postconfiguration, so it cannot copy the
     * dict (which is only finalised here).  We iterate cmcf->servers to
     * reach each server's own srv_conf and set its dict pointer directly.
     *
     * Only overwrite if not already set (a future per-server dict override
     * feature could set it earlier).
     */
    cscf = cmcf->servers.elts;
    for (i = 0; i < cmcf->servers.nelts; i++) {
        rcf = cscf[i]->ctx->srv_conf[ngx_stream_radius_module.ctx_index];
        if (rcf->dict == NULL) {
            rcf->dict = mcf->dict;
        }
    }

    /* Also set it on the main-level srv_conf for completeness */
    rcf = ngx_stream_conf_get_module_srv_conf(cf, ngx_stream_radius_module);
    if (rcf->dict == NULL) {
        rcf->dict = mcf->dict;
    }

    /* Register the preread phase handler (self-selects via rcf->enabled) */
    h = ngx_array_push(&cmcf->phases[NGX_STREAM_PREREAD_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_stream_radius_preread_handler;

    return NGX_OK;
}


/* =========================================================================
 * Variable registration (preconfiguration)
 * ====================================================================== */

static ngx_int_t
ngx_stream_radius_add_variables(ngx_conf_t *cf)
{
    ngx_stream_variable_t  *var, *v;

    for (v = ngx_stream_radius_vars; v->name.len; v++) {
        var = ngx_stream_add_variable(cf, &v->name, NGX_STREAM_VAR_NOCACHEABLE);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = v->get_handler;
        var->data        = v->data;
    }

    return NGX_OK;
}


/* =========================================================================
 * Preread handler
 *
 * UDP and TCP require different buffer handling:
 *
 *   UDP: NGINX reads the datagram from the server listening socket and
 *        places the bytes into c->buffer BEFORE ngx_stream_init_connection
 *        is called.  By the time our preread handler fires, c->buffer is
 *        already populated.  Calling c->recv() would try to read a SECOND
 *        datagram (none pending) and return NGX_AGAIN, stalling the session.
 *        We must read from c->buffer directly without calling recv.
 *
 *   TCP: Data arrives incrementally.  c->buffer starts NULL.  We allocate
 *        it and call c->recv() to accumulate bytes until a full RADIUS
 *        packet is buffered.
 * ====================================================================== */

static ngx_int_t
ngx_stream_radius_preread_handler(ngx_stream_session_t *s)
{
    ngx_stream_radius_srv_conf_t  *rcf;
    ngx_stream_radius_ctx_t       *ctx;
    ngx_connection_t              *c;
    ngx_buf_t                     *b;
    ssize_t                        n;
    size_t                         avail;
    ngx_int_t                      rc;

    rcf = ngx_stream_get_module_srv_conf(s, ngx_stream_radius_module);

    if (!rcf->enabled) {
        return NGX_DECLINED;
    }

    c = s->connection;

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_radius_module);
    if (ctx == NULL) {
        ctx = ngx_pcalloc(c->pool, sizeof(ngx_stream_radius_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }

        ctx->pool = c->pool;
        ctx->rcf  = rcf;
        ctx->dict = rcf->dict;

        ngx_stream_set_ctx(s, ctx, ngx_stream_radius_module);
    }

    if (c->type == SOCK_DGRAM) {
        /*
         * UDP: datagram is already in c->buffer.  Do not call recv.
         */
        b = c->buffer;

        if (b == NULL || b->pos >= b->last) {
            ngx_log_error(NGX_LOG_WARN, c->log, 0,
                          "radius: UDP c->buffer empty in preread");
            ctx->parse_error = 1;
            return NGX_OK;
        }

    } else {
        /*
         * TCP: allocate c->buffer if needed; accumulate until full packet.
         */
        b = c->buffer;

        if (b == NULL) {
            b = ngx_create_temp_buf(c->pool, rcf->buffer_size);
            if (b == NULL) {
                return NGX_ERROR;
            }
            c->buffer = b;
        }

        n = c->recv(c, b->last, b->end - b->last);

        if (n == NGX_AGAIN) {
            return NGX_AGAIN;
        }

        if (n == NGX_ERROR || n == 0) {
            return NGX_ERROR;
        }

        b->last += n;
    }

    avail = (size_t)(b->last - b->pos);

    if (avail < NGX_RADIUS_HEADER_LEN) {
        ngx_log_error(NGX_LOG_WARN, c->log, 0,
                      "radius: packet too short (%uz bytes)", avail);
        ctx->parse_error = 1;
        return NGX_OK;
    }

    /* Read the RADIUS Length field (header bytes 2–3) */
    ctx->pkt_len = ((size_t) b->pos[2] << 8) | (size_t) b->pos[3];

    if (ctx->pkt_len < NGX_RADIUS_HEADER_LEN
        || ctx->pkt_len > NGX_RADIUS_MAX_PACKET_LEN)
    {
        ngx_log_error(NGX_LOG_WARN, c->log, 0,
                      "radius: invalid Length field %uz", ctx->pkt_len);
        ctx->parse_error = 1;
        return NGX_OK;
    }

    if (avail < ctx->pkt_len) {
        /* TCP only: wait for the rest */
        if ((size_t)(b->end - b->pos) < ctx->pkt_len) {
            ngx_log_error(NGX_LOG_WARN, c->log, 0,
                          "radius: packet exceeds buffer; "
                          "raise radius_buffer_size");
            ctx->parse_error = 1;
            return NGX_OK;
        }
        return NGX_AGAIN;
    }

    rc = ngx_stream_radius_parse_packet(ctx, b->pos, ctx->pkt_len, c->log);
    if (rc != NGX_OK) {
        ctx->parse_error = 1;
    }

    ngx_log_debug3(NGX_LOG_DEBUG_STREAM, c->log, 0,
                   "radius: parsed code=%d id=%d attrs=%d",
                   ctx->code, ctx->identifier, ctx->nattrs);

    /*
     * Wrap c->send_chain to intercept the upstream response.
     *
     * After the preread phase, the proxy module forwards upstream data
     * to the downstream client by calling c->send_chain.  We wrap that
     * function pointer here: our wrapper peeks at the first byte of the
     * first buffer (the RADIUS response code), saves it in ctx, then
     * calls the original send_chain so forwarding proceeds unchanged.
     *
     * This gives us $radius_response_code / $radius_response_code_name
     * at log phase without touching any proxy-internal structures.
     */
    if (!ctx->parse_error && ctx->original_send_chain == NULL) {
        ctx->original_send_chain = c->send_chain;
        c->send_chain            = ngx_stream_radius_send_chain;
    }

    return NGX_OK;
}


/* =========================================================================
 * Variable getters — packet-level fields
 * ====================================================================== */

static ngx_int_t
ngx_stream_radius_variable(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_stream_radius_ctx_t  *ctx;
    u_char                   *p;

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_radius_module);

    if (ctx == NULL || ctx->parse_error) {
        v->not_found = 1;
        return NGX_OK;
    }

    switch (data) {

    case NGX_RADIUS_VAR_CODE:
        p = ngx_pnalloc(s->connection->pool, NGX_INT_T_LEN);
        if (p == NULL) { return NGX_ERROR; }
        v->len  = ngx_sprintf(p, "%d", ctx->code) - p;
        v->data = p;
        break;

    case NGX_RADIUS_VAR_CODE_NAME:
        v->data = (u_char *) ngx_stream_radius_code_name(ctx->code);
        v->len  = ngx_strlen(v->data);
        break;

    case NGX_RADIUS_VAR_IDENTIFIER:
        p = ngx_pnalloc(s->connection->pool, NGX_INT_T_LEN);
        if (p == NULL) { return NGX_ERROR; }
        v->len  = ngx_sprintf(p, "%d", ctx->identifier) - p;
        v->data = p;
        break;

    case NGX_RADIUS_VAR_LENGTH:
        p = ngx_pnalloc(s->connection->pool, NGX_INT_T_LEN);
        if (p == NULL) { return NGX_ERROR; }
        v->len  = ngx_sprintf(p, "%uz", ctx->pkt_len) - p;
        v->data = p;
        break;

    case NGX_RADIUS_VAR_AUTHENTICATOR:
        p = ngx_pnalloc(s->connection->pool, 32 + 1);
        if (p == NULL) { return NGX_ERROR; }
        v->len  = ngx_hex_dump(p, (u_char *) ctx->authenticator, 16) - p;
        v->data = p;
        break;

    case NGX_RADIUS_VAR_VALID:
        v->data = (u_char *)(ctx->parse_error ? "0" : "1");
        v->len  = 1;
        break;

    default:
        v->not_found = 1;
        return NGX_OK;
    }

    v->valid         = 1;
    v->no_cacheable  = 0;
    v->not_found     = 0;

    return NGX_OK;
}


/* =========================================================================
 * Variable getter — standard attribute by type code
 * ====================================================================== */

static ngx_int_t
ngx_stream_radius_attr_variable(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_stream_radius_ctx_t   *ctx;
    ngx_stream_radius_attr_t  *attr;
    ngx_uint_t                 i, type;

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_radius_module);

    if (ctx == NULL || ctx->parse_error) {
        v->not_found = 1;
        return NGX_OK;
    }

    type = (ngx_uint_t) data;
    attr = ctx->attrs;

    for (i = 0; i < ctx->nattrs; i++, attr++) {
        if (attr->type == type && attr->vendor_id == 0) {
            v->data         = attr->value.data;
            v->len          = attr->value.len;
            v->valid        = 1;
            v->not_found    = 0;
            v->no_cacheable = 0;
            return NGX_OK;
        }
    }

    v->not_found = 1;
    return NGX_OK;
}


/* =========================================================================
 * send_chain intercept — capture upstream RADIUS response code
 *
 * Called by the proxy module to forward the upstream response to the
 * downstream client.  We peek at the first byte (RADIUS code), store it
 * in our per-session ctx, then call the original send_chain unchanged so
 * the response is forwarded normally.
 *
 * c->data is ngx_stream_session_t* for downstream connections in the
 * NGINX stream module.
 * ====================================================================== */

static ngx_chain_t *
ngx_stream_radius_send_chain(ngx_connection_t *c, ngx_chain_t *in,
    off_t limit)
{
    ngx_stream_session_t     *s;
    ngx_stream_radius_ctx_t  *ctx;
    ngx_chain_t              *cl;
    ngx_buf_t                *b;
    size_t                    avail, gathered;
    ngx_stream_radius_ctx_t   resp_ctx;

    s   = c->data;
    ctx = ngx_stream_get_module_ctx(s, ngx_stream_radius_module);

    if (ctx != NULL && !ctx->response_parsed) {

        /*
         * Gather bytes from the chain into ctx->resp_buf.
         *
         * For UDP (proxy_responses 1) the upstream reply arrives as a
         * single buffer holding the entire datagram, so one iteration
         * suffices.  For TCP the chain may span multiple buffers; we
         * copy until we have a complete RADIUS packet or fill the buffer.
         */
        gathered = 0;
        for (cl = in; cl != NULL; cl = cl->next) {
            b = cl->buf;
            if (b == NULL) {
                continue;
            }
            avail = (size_t)(b->last - b->pos);
            if (avail == 0) {
                continue;
            }
            if (gathered + avail > sizeof(ctx->resp_buf)) {
                avail = sizeof(ctx->resp_buf) - gathered;
            }
            ngx_memcpy(ctx->resp_buf + gathered, b->pos, avail);
            gathered += avail;

            /* Stop once we have a complete RADIUS packet */
            if (gathered >= 4) {
                size_t declared = ((size_t) ctx->resp_buf[2] << 8)
                                |  (size_t) ctx->resp_buf[3];
                if (declared >= 20 && declared <= sizeof(ctx->resp_buf)
                    && gathered >= declared)
                {
                    gathered = declared;
                    break;
                }
            }
        }

        if (gathered >= 20) {
            /*
             * Parse the response into a temporary context so we can
             * re-use the existing parser without touching request attrs.
             * We then copy header fields + attrs into ctx->resp_*.
             */
            ngx_memzero(&resp_ctx, sizeof(resp_ctx));
            resp_ctx.pool = ctx->pool;
            resp_ctx.dict = ctx->dict;
            resp_ctx.rcf  = ctx->rcf;

            if (ngx_stream_radius_parse_packet(&resp_ctx, ctx->resp_buf,
                                               gathered, c->log) == NGX_OK)
            {
                ctx->response_code   = resp_ctx.code;
                ctx->resp_pkt_len    = resp_ctx.pkt_len;

                /* Copy parsed attributes (they point into pool allocs) */
                ctx->resp_nattrs = resp_ctx.nattrs;
                if (ctx->resp_nattrs > NGX_RADIUS_MAX_ATTRS) {
                    ctx->resp_nattrs = NGX_RADIUS_MAX_ATTRS;
                }
                ngx_memcpy(ctx->resp_attrs, resp_ctx.attrs,
                           ctx->resp_nattrs * sizeof(ngx_stream_radius_attr_t));
            } else {
                /* Still record the code even if full parse failed */
                ctx->response_code = ctx->resp_buf[0];
            }

            ctx->response_parsed = 1;

            ngx_log_debug2(NGX_LOG_DEBUG_STREAM, c->log, 0,
                           "radius: response code=%d attrs=%uz",
                           ctx->response_code, ctx->resp_nattrs);
        }
    }

    /* Forward the response to the client unchanged */
    return ctx->original_send_chain(c, in, limit);
}


/* =========================================================================
 * Variable getter — upstream response packet fields
 * ====================================================================== */

static ngx_int_t
ngx_stream_radius_resp_variable(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_stream_radius_ctx_t  *ctx;
    u_char                   *p;

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_radius_module);

    if (ctx == NULL || !ctx->response_parsed) {
        v->not_found = 1;
        return NGX_OK;
    }

    switch (data) {

    case NGX_RADIUS_VAR_RESP_CODE:
        p = ngx_pnalloc(s->connection->pool, NGX_INT_T_LEN);
        if (p == NULL) { return NGX_ERROR; }
        v->len  = ngx_sprintf(p, "%d", ctx->response_code) - p;
        v->data = p;
        break;

    case NGX_RADIUS_VAR_RESP_NAME:
        v->data = (u_char *) ngx_stream_radius_code_name(ctx->response_code);
        v->len  = ngx_strlen(v->data);
        break;

    default:
        v->not_found = 1;
        return NGX_OK;
    }

    v->valid        = 1;
    v->no_cacheable = 0;
    v->not_found    = 0;

    return NGX_OK;
}


/* =========================================================================
 * Variable getter — response packet attributes by type code
 *
 * Mirrors ngx_stream_radius_attr_variable but reads from ctx->resp_attrs
 * (populated by the send_chain intercept) instead of the request attrs.
 * ====================================================================== */

static ngx_int_t
ngx_stream_radius_resp_attr_variable(ngx_stream_session_t *s,
    ngx_stream_variable_value_t *v, uintptr_t data)
{
    ngx_stream_radius_ctx_t   *ctx;
    ngx_stream_radius_attr_t  *attr;
    ngx_uint_t                 i, type;

    ctx = ngx_stream_get_module_ctx(s, ngx_stream_radius_module);

    if (ctx == NULL || !ctx->response_parsed) {
        v->not_found = 1;
        return NGX_OK;
    }

    type = (ngx_uint_t) data;
    attr = ctx->resp_attrs;

    for (i = 0; i < ctx->resp_nattrs; i++, attr++) {
        if (attr->type == type && attr->vendor_id == 0) {
            v->data         = attr->value.data;
            v->len          = attr->value.len;
            v->valid        = 1;
            v->not_found    = 0;
            v->no_cacheable = 0;
            return NGX_OK;
        }
    }

    v->not_found = 1;
    return NGX_OK;
}
