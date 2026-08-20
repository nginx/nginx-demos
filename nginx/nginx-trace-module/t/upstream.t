# vi:set ft=perl ts=4 sw=4 et:
#
# M3.1-M3.4 — structured per-try upstream capture in the committed transaction.
#
# When a traced request proxies upstream, the committed JSON gains an
# "upstream" section with a "tries" array. Each try carries the byte-exact
# sent request (M3.1), the raw received response header block (M3.2), the
# parsed HTTP status plus u->state timing/bytes (M3.3), and — when
# `trace_upstream_capture off` — the section degrades away (M3.4 / FR-UP-7).
#
# These assertions read the committed transaction back through the control
# endpoint, so they exercise the full capture -> commit -> shm -> read path.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();

run_tests();

__DATA__

=== TEST 1: proxied request produces an upstream section with one try
--- http_config
    trace_zone zup1 1m;
--- config
    location = /u1 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/u1back;
    }
    location = /u1back { return 200 "u1"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /u1", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["u1", qr/"upstream"\s*:\s*\{.*"tries"\s*:\s*\[\s*\{.*"seq"\s*:\s*0/s]

=== TEST 2: the captured try holds the byte-exact sent request line
# proxy_pass sends "GET /u2back HTTP/1.0" upstream; that exact request line
# must appear inside the try's "request" field (M3.1, FR-UP-2).
--- http_config
    trace_zone zup2 1m;
--- config
    location = /u2 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/u2back;
    }
    location = /u2back { return 200 "u2"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /u2", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["u2", qr/"request"\s*:\s*"GET \/u2back HTTP\/1\.0/]

=== TEST 3: the try records the received response header block and status
# The raw upstream response header block (starting "HTTP/1.1 201") is snapshot
# into "response_headers" and the parsed status is surfaced (M3.2/M3.3). The
# try's own "status":201 precedes response_headers in the object, so assert both
# in that order.
--- http_config
    trace_zone zup3 1m;
--- config
    location = /u3 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/u3back;
    }
    location = /u3back { return 201 "u3"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /u3", "GET /trace/last"]
--- error_code eval
[201, 200]
--- response_body_like eval
["u3", qr/"status"\s*:\s*201.*"response_headers"\s*:\s*"HTTP\/1\.1 201/s]

=== TEST 4: trace_upstream_capture off degrades the section away (FR-UP-7)
# With capture off, the request still proxies and is traced, but no byte-exact
# upstream section is emitted — the "upstream" key is absent entirely.
--- http_config
    trace_zone zup4 1m;
--- config
    location = /u4 {
        trace on;
        trace_upstream_capture off;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/u4back;
    }
    location = /u4back { return 200 "u4"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /u4", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["u4", qr/"txn"\s*:\s*"trace"/]
--- response_body_unlike eval
[qr/NOMATCH_SENTINEL/, qr/"upstream"\s*:/]

=== TEST 5: a non-proxied traced request has no upstream section
# A pure content-handler (return) request never goes upstream, so no tries are
# captured and the "upstream" key must be omitted (section is upstream-only).
--- http_config
    trace_zone zup5 1m;
--- config
    location = /u5 {
        trace on;
        return 200 "u5";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /u5", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["u5", qr/"uri"\s*:\s*"\/u5"/]
--- response_body_unlike eval
[qr/NOMATCH_SENTINEL/, qr/"upstream"\s*:/]

=== TEST 6: the try carries the state-derived byte count and truncation flags
# Beyond status, the try model surfaces u->state bytes ("bytes") and the two
# capture-cap flags ("request_truncated"/"response_truncated"). A small response
# fits under the 1 KiB caps, so both flags are false and "bytes" is present —
# proving the full per-try shape (M3.2 caps + M3.3 state harvest), not just status.
--- http_config
    trace_zone zup6 1m;
--- config
    location = /u6 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/u6back;
    }
    location = /u6back { return 200 "u6"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /u6", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["u6", qr/"bytes"\s*:\s*\d+.*"request_truncated"\s*:\s*false.*"response_truncated"\s*:\s*false/s]

=== TEST 7: a non-2xx upstream response is captured with its real status
# Edge: prior tests only proxy 200/201 backends. Here the upstream returns 503;
# the try must surface the upstream's OWN status (503) and its raw
# "HTTP/1.1 503" response header block — proving capture is status-agnostic and
# records upstream faults verbatim, not just success responses.
--- http_config
    trace_zone zup7 1m;
--- config
    location = /u7 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/u7back;
    }
    location = /u7back { return 503 "upstream-down"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /u7", "GET /trace/last"]
--- error_code eval
[503, 200]
# the try records status 503 and the raw 503 status line in response_headers.
--- response_body_like eval
[qr//, qr/"status"\s*:\s*503.*"response_headers"\s*:\s*"HTTP\/1\.1 503/s]
