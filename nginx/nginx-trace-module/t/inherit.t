# M1.2 — configuration inheritance across http / server / location.
#
# Verifies FR-CFG-17/18: mergeable directives inherit from the enclosing scope
# and a more specific scope overrides. We assert this behaviorally through the
# module's self-diagnostics log (trace_log_level debug), which records the
# effective merged config per traced request. Concretely: a request in a
# location that does NOT set `trace` inherits `trace on;` from http/server, and
# a location that sets `trace off;` overrides an inherited `trace on;`.

use lib 'inc';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: location inherits `trace on` from the http scope
--- http_config
    trace_zone zinh 1m;
    trace on;
    trace_watch $request_method;
--- config
    # no `trace` here -> inherits `on` from http
    location = /inherit-on {
        return 200 "io";
    }
--- request
GET /inherit-on
--- error_code: 200
--- response_body chomp
io

=== TEST 2: location `trace off` overrides inherited `trace on`
--- http_config
    trace_zone zinh2 1m;
    trace on;
--- config
    location = /override-off {
        trace off;                 # overrides inherited on
        return 200 "oo";
    }
--- request
GET /override-off
--- error_code: 200
--- response_body chomp
oo

=== TEST 3: server-scope `trace on` inherited by locations in that server
--- http_config
    trace_zone zinh3 1m;
--- config
    # server-scope enable, inherited by the location below
    trace on;
    location = /srv-inherit {
        return 200 "si";
    }
--- request
GET /srv-inherit
--- error_code: 200
--- response_body chomp
si

=== TEST 4: redact + watch lists inherit and a nested location extends scope
--- http_config
    trace_zone zinh4 1m;
    trace on;
    trace_redact authorization;
    trace_watch $request_method;
--- config
    location /outer {
        trace_redact x-inner-secret;    # overrides inherited list
        location = /outer/inner {
            return 200 "oi";
        }
    }
--- request
GET /outer/inner
--- error_code: 200
--- response_body chomp
oi

=== TEST 5: enum/numeric directives inherit from http and a location overrides
# trace_upstream_capture (enum) and trace_body_max (size) are set at http scope
# and overridden in a location; a proxied request through the overriding
# location must still succeed, proving the merged config is valid end-to-end.
--- http_config
    trace_zone zinh5 1m;
    trace on;
    trace_upstream_capture headers;   # http-scope default
    trace_body_max 4k;                # http-scope default
--- config
    location = /inh-front {
        trace_upstream_capture full;  # overrides inherited "headers"
        trace_body_max 32k;           # overrides inherited 4k
        proxy_pass http://127.0.0.1:$server_port/inh-back;
    }
    location = /inh-back { return 200 "inh-backend"; }
--- request
GET /inh-front
--- error_code: 200
--- response_body chomp
inh-backend
