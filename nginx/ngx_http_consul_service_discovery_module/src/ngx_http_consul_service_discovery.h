/*
 * ngx_http_consul_service_discovery.h
 *
 * Public header for the NGINX Consul Service Discovery module.
 * Include this file from the module implementation and any companion modules
 * that need to inspect the service catalogue.
 *
 * Copyright (c) 2024 – present, Contributors
 * Licensed under the MIT License.  See LICENSE for details.
 */

#ifndef _NGX_HTTP_CONSUL_SERVICE_DISCOVERY_H_INCLUDED_
#define _NGX_HTTP_CONSUL_SERVICE_DISCOVERY_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#define NGX_HTTP_CONSUL_DISCOVERY_MODULE  "ngx_http_consul_service_discovery"
#define NGX_HTTP_CONSUL_DISCOVERY_VERSION "1.1.0"


/*
 * Per-server{} configuration.
 *
 * Directives: consul_service_discoverable, consul_service_name,
 *             consul_service_tags, consul_service_port_override.
 *
 * Port and address are now auto-discovered:
 *   port    — read from the listen directive at postconfiguration time by
 *             walking cmcf->ports; consul_service_port_override overrides it.
 *   address — taken from the first server_name value (cscf->server_name).
 */
typedef struct {
    ngx_flag_t   discoverable;    /* consul_service_discoverable on|off      */
    ngx_str_t    service_name;    /* consul_service_name "<name>"            */
    ngx_str_t    tags;            /* consul_service_tags "t1,t2,..."         */
    ngx_uint_t   port_override;   /* consul_service_port_override <port>     */
} ngx_http_consul_service_discovery_srv_conf_t;


/*
 * A single snapshotted service entry collected at postconfiguration time.
 * Stored in the main conf's services array.
 *
 * Fields:
 *   name — consul_service_name, or first server_name value
 *   addr — bind IP from the listen directive ("0.0.0.0", "1.2.3.4", "::", etc.)
 *   port — port from the listen directive, or consul_service_port_override
 *   tags — consul_service_tags, or "nginx"
 */
typedef struct {
    ngx_str_t    name;
    ngx_str_t    addr;
    ngx_uint_t   port;
    ngx_str_t    tags;
} ngx_http_consul_service_t;


/*
 * Main (http{}) configuration.
 * Holds the global service catalogue snapshotted during postconfiguration.
 */
typedef struct {
    ngx_array_t  *services;  /* array of ngx_http_consul_service_t entries  */
    ngx_uint_t    updated;   /* incremented each time the catalogue is built */
} ngx_http_consul_service_discovery_main_conf_t;


/*
 * Per-location{} configuration.
 * Directive: consul_service_discovery on|off.
 */
typedef struct {
    ngx_flag_t   enable;     /* consul_service_discovery on|off             */
} ngx_http_consul_service_discovery_loc_conf_t;


extern ngx_module_t  ngx_http_consul_service_discovery_module;

#endif /* _NGX_HTTP_CONSUL_SERVICE_DISCOVERY_H_INCLUDED_ */
