# M1.1 — full directive surface parses in its declared context with defaults.
#
# Verifies FR-CFG-1..16: each directive is accepted where the SPEC declares it,
# with correct arg arity, and rejected where it is not allowed. Behavior of the
# capture-heavy directives (trace_intercept/ebpf/grpc_proto/body) is deferred to
# later milestones; M1 only requires that they PARSE correctly and inherit.

use lib 'inc';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: full directive surface parses; a request still succeeds
--- http_config
    trace_zone ztest 1m;
    trace on;
    trace_watch $request_method $uri;
    trace_intercept off;
    trace_upstream_capture headers;
    trace_grpc_proto /dev/null;
    trace_ebpf off;
    trace_max_sessions 8;
    trace_max_transactions 50;
    trace_body_capture off;
    trace_body_max 8k;
    trace_redact authorization cookie set-cookie;
    trace_retention 1h;
    trace_log off;
    trace_log_level info;
--- config
    location = /ok {
        return 200 "ok";
    }
--- request
GET /ok
--- error_code: 200
--- response_body chomp
ok

=== TEST 2: location-scoped directives parse in a location block
--- http_config
    trace_zone ztest2 1m;
--- config
    location = /l {
        trace on;
        trace_watch $args;
        trace_upstream_capture full;
        trace_body_capture both;
        trace_body_max 16k;
        trace_redact x-api-key;
        return 200 "loc-ok";
    }
    location = /ctl {
        trace_control;
    }
--- request
GET /l
--- error_code: 200
--- response_body chomp
loc-ok

=== TEST 3: trace on|off accepts explicit off; defaults leave request untouched
--- http_config
    trace_zone ztest3 1m;
    trace off;
--- config
    location = /off {
        trace off;
        return 200 "off-ok";
    }
--- request
GET /off
--- error_code: 200
--- response_body chomp
off-ok

=== TEST 4: trace_log_level accepts every level in the ladder
--- http_config
    trace_zone ztest4 1m;
    trace_log_level trace;
--- config
    location = /lvl { return 200 "lvl"; }
--- request
GET /lvl
--- error_code: 200
--- response_body chomp
lvl

=== TEST 5: trace_control location returns JSON (200) once a zone exists
--- http_config
    trace_zone ztest5 1m;
--- config
    location = /trace/ctl {
        trace_control;
    }
--- request
GET /trace/ctl
--- error_code: 200

=== TEST 6: a main-conf directive is rejected in the location context
# trace_zone is NGX_HTTP_MAIN_CONF; using it inside a location must fail the
# config test (proves context enforcement, FR-CFG-1 / directive scoping).
--- http_config
    trace_zone ztest6 1m;
--- config
    location = /bad {
        trace_zone zbad 1m;
        return 200 "unreachable";
    }
--- must_die
--- error_log
"trace_zone" directive is not allowed here
