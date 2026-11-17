# vi:set ft=perl ts=4 sw=4 et:
#
# M4.1 / M4.2 — fault capture (summary.fault) and fault determination at LOG.
#
# On a denied/errored request the module populates summary.fault with phase,
# handler, code, status, error_state, message, and a step_seq linking to the
# exact failing step (FR-FAULT-1). The fault is fully determined at LOG so it
# can gate fault-only commit (FR-FAULT-2). AC-9 is the auth_request 401 case.
# Everything is read back from the committed transaction JSON.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: AC-9 — auth_request 401 populates summary.fault + step_seq
--- http_config
    trace_zone zf1 1m;
--- config
    location = /guarded1 {
        trace on;
        auth_request /auth1;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/backend1;
    }
    location = /auth1 {
        return 401;
    }
    location = /backend1 {
        return 200 "never";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /guarded1", "GET /trace/last"]
--- error_code eval
[401, 200]
--- response_body_like eval
[qr//, qr/"fault"\s*:\s*\{.*"status"\s*:\s*401.*"step_seq"\s*:\s*\d+/s]

=== TEST 2: fault carries error_state=access_denied + a message
--- http_config
    trace_zone zf2 1m;
--- config
    location = /guarded2 {
        trace on;
        auth_request /auth2;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/backend2;
    }
    location = /auth2 {
        return 403;
    }
    location = /backend2 {
        return 200 "never";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /guarded2", "GET /trace/last"]
--- error_code eval
[403, 200]
--- response_body_like eval
[qr//, qr/"error_state"\s*:\s*"access_denied".*"message"\s*:\s*"request denied"/s]

=== TEST 3: a 5xx yields error_state=server_error
--- http_config
    trace_zone zf3 1m;
--- config
    location = /boom3 {
        trace on;
        return 500;
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /boom3", "GET /trace/last"]
--- error_code eval
[500, 200]
--- response_body_like eval
[qr//, qr/"fault"\s*:\s*\{.*"status"\s*:\s*500.*"error_state"\s*:\s*"server_error"/s]

=== TEST 4: a successful request has NO fault section
--- http_config
    trace_zone zf4 1m;
--- config
    location = /ok4 {
        trace on;
        return 200 "ok4";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /ok4", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_unlike eval
[qr/NOPE/, qr/"fault"/]

=== TEST 5: fault.step_seq matches a step that is marked error
--- http_config
    trace_zone zf5 1m;
--- config
    location = /boom5 {
        trace on;
        return 502;
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /boom5", "GET /trace/last"]
--- error_code eval
[502, 200]
--- response_body_like eval
[qr//, qr/"status"\s*:\s*"error".*"fault".*"step_seq"\s*:\s*\d+/s]

=== TEST 6: 404 yields error_state=not_found
--- http_config
    trace_zone zf6 1m;
--- config
    trace on;
    location = /present6 {
        return 200 "here";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /missing6", "GET /trace/last"]
--- error_code eval
[404, 200]
--- response_body_like eval
[qr//, qr/"status"\s*:\s*404.*"error_state"\s*:\s*"not_found"/s]

=== TEST 7: M4.1 — an upstream 5xx yields error_state=upstream_error
# Distinct from TEST 3's directive-forced 500: here the failure originates in a
# real upstream try, so detect_fault's upstream_failed branch must classify it
# as upstream_error (and the captured try records the 5xx).
--- http_config
    trace_zone zf7 1m;
--- config
    location = /up7 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/backend7;
    }
    location = /backend7 {
        return 502;
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /up7", "GET /trace/last"]
--- error_code eval
[502, 200]
--- response_body_like eval
[qr//, qr/"fault"\s*:\s*\{.*"error_state"\s*:\s*"upstream_error".*"message"\s*:\s*"upstream error"/s]

=== TEST 8: M4.2 — a 4xx client error is determined at LOG (error_state=client_error)
# A non-404 4xx exercises the client_error branch and proves the fault is fully
# resolved at LOG (status < 500, not access/not_found).
--- http_config
    trace_zone zf8 1m;
--- config
    location = /bad8 {
        trace on;
        return 400;
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /bad8", "GET /trace/last"]
--- error_code eval
[400, 200]
--- response_body_like eval
[qr//, qr/"fault"\s*:\s*\{.*"status"\s*:\s*400.*"error_state"\s*:\s*"client_error"/s]
