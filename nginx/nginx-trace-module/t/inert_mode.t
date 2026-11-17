# M1.3 — inert mode: no `trace_zone` => config still loads, but capture is
# disabled and the control API returns 503 (CON-CFG-1, FR-API-12).

use lib 'inc';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: directives accepted with NO trace_zone; request still succeeds
--- http_config
    # deliberately NO trace_zone; all directives must still be accepted
    trace on;
    trace_watch $uri;
    trace_upstream_capture full;
    trace_body_capture both;
--- config
    location = /noz { return 200 "no-zone-ok"; }
--- request
GET /noz
--- error_code: 200
--- response_body chomp
no-zone-ok

=== TEST 2: control endpoint returns 503 when no zone is configured
--- http_config
    # no trace_zone
--- config
    location = /trace/ctl {
        trace_control;
    }
--- request
GET /trace/ctl
--- error_code: 503

=== TEST 3: with a zone, the same control endpoint is available (not 503)
--- http_config
    trace_zone zpresent 1m;
--- config
    location = /trace/ctl {
        trace_control;
    }
--- request
GET /trace/ctl
--- error_code: 200

=== TEST 4: inert mode is a true no-op — `trace on` request commits nothing
# With NO zone but debug diagnostics on, a traced request must still succeed
# and MUST NOT emit the commit milestone (capture is fully disabled without a
# zone; CON-CFG-1). Proves inert mode short-circuits before commit.
--- http_config
    # no trace_zone
    trace_log_level debug;
--- config
    location = /front {
        trace on;
        proxy_pass http://127.0.0.1:$server_port/back;
    }
    location = /back { return 200 "backend"; }
--- request
GET /front
--- error_code: 200
--- response_body chomp
backend
--- no_error_log
[trace] commit txn
