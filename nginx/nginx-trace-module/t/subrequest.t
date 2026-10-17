# vi:set ft=perl ts=4 sw=4 et:
#
# M8.4 — subrequest correlation.
#
# Before M8 a subrequest had no trace context at all, so an auth_request was
# invisible in the timeline: the parent showed a 401 with no indication of which
# internal request produced it. The header filter now records each subrequest as
# a `subrequest`-typed step on the MAIN request's timeline, carrying the
# subrequest URI and its own status.
#
# The properties asserted here:
#   - a subrequest appears as a step under its parent, not as its own transaction;
#   - it is attributed to the correct URI, so fan-out is distinguishable;
#   - a failing subrequest is marked `error`, which is the signal an operator is
#     actually looking for;
#   - correlation does not change routing or the client-visible response (G1).

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: an auth_request subrequest appears as a subrequest step
--- http_config
    trace_zone zs1 1m;
--- config
    location = /s1 {
        trace on;
        auth_request /authz1;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/backend1;
    }
    location = /authz1 { return 204; }
    location = /backend1 { return 200 "ok1"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /s1", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/"phase"\s*:\s*"SUBREQUEST".*"type"\s*:\s*"subrequest"/s]

=== TEST 2: the step names the subrequest's own URI
# Without the URI the step is useless on a page with several subrequests.
--- http_config
    trace_zone zs2 1m;
--- config
    location = /s2 {
        trace on;
        auth_request /authz2;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/backend2;
    }
    location = /authz2 { return 204; }
    location = /backend2 { return 200 "ok2"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /s2", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/"type"\s*:\s*"subrequest".*"note"\s*:\s*"\/authz2"/s]

=== TEST 3: a DENYING auth_request subrequest is marked error
# The 401 case is the whole reason this feature exists: the parent's fault says
# "denied at ACCESS", and the subrequest step says which internal request denied
# it. Note the subrequest's own status is 401 while the client also sees 401.
--- http_config
    trace_zone zs3 1m;
--- config
    location = /s3 {
        trace on;
        auth_request /authz3;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/backend3;
    }
    location = /authz3 { return 401; }
    location = /backend3 { return 200 "never3"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /s3", "GET /trace/last"]
--- error_code eval
[401, 200]
--- response_body_like eval
[qr//, qr/"type"\s*:\s*"subrequest"[^}]*"status"\s*:\s*"error"|"status"\s*:\s*"error"[^}]*"type"\s*:\s*"subrequest"/s]

=== TEST 4: the subrequest step and the parent's fault appear in ONE transaction
# Correlation means exactly this: the subrequest is not a separate committed
# transaction, it is a step inside the parent's. Assert both are in one document.
--- http_config
    trace_zone zs4 1m;
--- config
    location = /s4 {
        trace on;
        auth_request /authz4;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/backend4;
    }
    location = /authz4 { return 403; }
    location = /backend4 { return 200 "never4"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /s4", "GET /trace/last"]
--- error_code eval
[403, 200]
--- response_body_like eval
[qr//, qr/"uri"\s*:\s*"\/s4".*"type"\s*:\s*"subrequest".*"fault".*"error_state"\s*:\s*"access_denied"/s]

=== TEST 5: G1 — correlation does not alter routing or the response
# The header filter records and then hands off unchanged; an allowed request must
# still reach its backend and return the backend's body verbatim.
--- http_config
    trace_zone zs5 1m;
--- config
    location = /s5 {
        trace on;
        auth_request /authz5;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/backend5;
    }
    location = /authz5 { return 204; }
    location = /backend5 { return 200 "backend-body-5"; }
--- request
GET /s5
--- error_code: 200
--- response_body chomp
backend-body-5

=== TEST 6: an untraced parent records no subrequest steps (G2 fast path)
# The filter must early-return on a non-traced request; nothing should be
# committed at all here.
--- http_config
    trace_zone zs6 1m;
--- config
    location = /s6 {
        auth_request /authz6;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/backend6;
    }
    location = /authz6 { return 204; }
    location = /backend6 { return 200 "ok6"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /s6", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_unlike eval
[qr/(?!)/, qr/SUBREQUEST/]

=== TEST 7: the subrequest's response body does not pollute the parent's preview
# The body filter skips subrequests deliberately (skill:filter-check-subrequest).
# Without that check, an auth_request's body would be captured as if it were the
# client-facing response. Assert the parent's preview is the BACKEND's body.
--- http_config
    trace_zone zs7 1m;
--- config
    location = /s7 {
        trace on;
        trace_body_capture response;
        auth_request /authz7;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/backend7;
    }
    location = /authz7 { return 200 "AUTHBODY7"; }
    location = /backend7 { return 200 "REALBODY7"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /s7", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/"response_body"[^}]*"preview"\s*:\s*"REALBODY7"/]
--- response_body_unlike eval
[qr/(?!)/, qr/AUTHBODY7/]
