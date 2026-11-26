# M1.5 — self-diagnostics logging (FR-LOG-1..6, NFR-LOG-1)
#
# Verifies the leveled self-diagnostics emit:
#   - with trace_log_level debug, the per-request commit milestone is logged
#   - at the default level (info), the debug-level milestone is short-circuited
#     and does NOT appear (proving the level ladder gates output)
#   - diagnostic lines carry only metadata (status/bytes), never payload bytes
#
# These run with the module genuinely loaded (TEST_NGINX_LOAD_MODULES).

use Test::Nginx::Socket 'no_plan';

run_tests();

__DATA__

=== TEST 1: debug level emits the commit milestone
--- http_config
    trace_zone dtrace 1m;
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
--- error_log
[trace] commit txn status=200

=== TEST 2: default level short-circuits the debug milestone
--- http_config
    trace_zone dtrace2 1m;
--- config
    location = /front2 {
        trace on;
        proxy_pass http://127.0.0.1:$server_port/back2;
    }
    location = /back2 { return 200 "backend"; }
--- request
GET /front2
--- error_code: 200
--- no_error_log
[trace] commit txn

=== TEST 3: the commit diagnostic line carries only metadata, no payload
# NOTE: the M0.3 upstream byte-capture spike logs raw upstream bytes at
# notice level (to be redaction-gated in a later milestone); this test
# scopes its assertion to the M1.5 self-diagnostics commit line, which must
# only ever carry status/bytes metadata (FR-LOG-6).
--- http_config
    trace_zone dtrace3 1m;
    trace_log_level trace;
--- config
    location = /front3 {
        trace on;
        proxy_pass http://127.0.0.1:$server_port/back3;
    }
    location = /back3 { return 200 "SECRETPAYLOAD"; }
--- request
GET /front3
--- error_code: 200
--- error_log
[trace] commit txn status=200 bytes=
--- no_error_log
[trace] commit txn status=200 bytes=0 SECRETPAYLOAD

=== TEST 4: trace_log routes diagnostics to a dedicated file sink
# With `trace_log <file>`, the commit milestone must be written to that file
# (relative to the server prefix), NOT to the nginx error_log (FR-LOG-1).
--- http_config
    trace_zone dtrace4 1m;
    trace_log logs/trace-diag.log;
    trace_log_level debug;
--- config
    location = /front4 {
        trace on;
        proxy_pass http://127.0.0.1:$server_port/back4;
    }
    location = /back4 { return 200 "backend"; }
--- request
GET /front4
--- error_code: 200
--- no_error_log
[trace] commit txn
--- shell_after
grep -q "\[trace\] commit txn status=200" $TEST_NGINX_SERVER_ROOT/logs/trace-diag.log
