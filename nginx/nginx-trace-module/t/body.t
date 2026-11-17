# vi:set ft=perl ts=4 sw=4 et:
#
# M8.1 / M8.2 / M8.3 — request & response body capture (AC-6, FR-BODY-1..5).
#
# The design decisions these tests lock in:
#   - capture is OFF by default (NFR-SEC-4): TEST 1 proves a traced request with
#     no trace_body_capture emits no body sections at all;
#   - request capture never forces a read (FR-BODY-2), so a location that does
#     not consume the body yields an empty preview rather than nginx changing
#     behavior to satisfy the tracer;
#   - the preview is bounded by min(trace_body_max, HARD_MAX) and reports honest
#     total_bytes/truncated accounting (FR-BODY-3, G3);
#   - the response is byte-identical whether or not capture is on (G1).

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: NFR-SEC-4 — no body sections when trace_body_capture is unset
--- http_config
    trace_zone zb1 1m;
--- config
    location = /b1 {
        trace on;
        return 200 "hello-b1";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /b1", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_unlike eval
[qr/(?!)/, qr/response_body|request_body/]

=== TEST 2: AC-6 — the response body preview is captured when enabled
--- http_config
    trace_zone zb2 1m;
--- config
    location = /b2 {
        trace on;
        trace_body_capture response;
        return 200 "response-payload-b2";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /b2", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/"response_body"\s*:\s*\{[^}]*"preview"\s*:\s*"response-payload-b2"/]

=== TEST 3: the preview carries captured_bytes / total_bytes / truncated
--- http_config
    trace_zone zb3 1m;
--- config
    location = /b3 {
        trace on;
        trace_body_capture response;
        return 200 "1234567890";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /b3", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/"response_body"\s*:\s*\{\s*"captured_bytes"\s*:\s*10\s*,\s*"total_bytes"\s*:\s*10\s*,\s*"truncated"\s*:\s*false/]

=== TEST 4: FR-BODY-3 — a body over trace_body_max is truncated, not dropped,
# and total_bytes still reports the true length so the operator knows what they
# are missing. 301 bytes captured at a 16-byte budget.
--- http_config
    trace_zone zb4 1m;
--- config
    location = /b4 {
        trace on;
        trace_body_capture response;
        trace_body_max 16;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/big4;
    }
    location = /big4 {
        return 200 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /b4", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/"response_body"\s*:\s*\{\s*"captured_bytes"\s*:\s*16\s*,\s*"total_bytes"\s*:\s*301\s*,\s*"truncated"\s*:\s*true/]

=== TEST 5: G1 — enabling capture does not alter the response bytes
# The body filter must be read-only: same body, same length, with capture on.
--- http_config
    trace_zone zb5 1m;
--- config
    location = /b5 {
        trace on;
        trace_body_capture both;
        return 200 "exactly-these-bytes";
    }
--- request
GET /b5
--- error_code: 200
--- response_body chomp
exactly-these-bytes

=== TEST 6: the response Content-Type is recorded alongside the preview
--- http_config
    trace_zone zb6 1m;
--- config
    location = /b6 {
        trace on;
        trace_body_capture response;
        default_type application/json;
        return 200 "{\"k\":1}";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /b6", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/"content_type"\s*:\s*"application\/json"/]

=== TEST 7: FR-BODY-1 — a POST body read by proxy_pass is captured
--- http_config
    trace_zone zb7 1m;
--- config
    location = /b7 {
        trace on;
        trace_body_capture request;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/sink7;
    }
    location = /sink7 { return 200 "sunk"; }
    location = /trace/last { trace_control; }
--- request eval
["POST /b7
posted-body-b7", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/"request_body"\s*:\s*\{[^}]*"preview"\s*:\s*"posted-body-b7"/]

=== TEST 8: `both` captures the two directions independently
--- http_config
    trace_zone zb8 1m;
--- config
    location = /b8 {
        trace on;
        trace_body_capture both;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/echo8;
    }
    location = /echo8 { return 200 "resp-side-b8"; }
    location = /trace/last { trace_control; }
--- request eval
["POST /b8
req-side-b8", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/"request_body".*"preview"\s*:\s*"req-side-b8".*"response_body".*"preview"\s*:\s*"resp-side-b8"/s]

=== TEST 9: `request` alone does not capture the response side
# Proves the direction bits are honoured independently rather than as one switch.
--- http_config
    trace_zone zb9 1m;
--- config
    location = /b9 {
        trace on;
        trace_body_capture request;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/echo9;
    }
    location = /echo9 { return 200 "resp-not-captured-b9"; }
    location = /trace/last { trace_control; }
--- request eval
["POST /b9
req-only-b9", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/"request_body"/]
--- response_body_unlike eval
[qr/(?!)/, qr/"response_body"/]

=== TEST 10: FR-BODY-2 — a location that never reads the body captures nothing
# `return 200` does not consume the request body, and we must NOT force a read to
# make the trace prettier. Nothing captured is the correct, spec-mandated result.
--- http_config
    trace_zone zb10 1m;
--- config
    location = /b10 {
        trace on;
        trace_body_capture request;
        return 200 "ignored";
    }
    location = /trace/last { trace_control; }
--- request eval
["POST /b10
never-read-b10", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_unlike eval
[qr/(?!)/, qr/never-read-b10/]

=== TEST 11: G3 — trace_body_max above the hard ceiling is clamped
# `trace_body_max 1m` must not let one request pin a megabyte: the effective
# budget is capped at BODY_HARD_MAX (2048), so a 3000-byte body truncates there.
--- http_config
    trace_zone zb11 1m;
--- config
    location = /b11 {
        trace on;
        trace_body_capture response;
        trace_body_max 1m;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/big11;
    }
    location = /big11 {
        # 3000 bytes of 'b'
        set $chunk "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
        return 200 "$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk$chunk";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /b11", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/"captured_bytes"\s*:\s*2048\s*,\s*"total_bytes"\s*:\s*3000\s*,\s*"truncated"\s*:\s*true/]

=== TEST 12: FR-BODY-4 — a gzipped response reports content_encoding, and the
# preview is the PRE-compression body.
#
# This pins down a deliberate design choice. Our filters sit at the head of the
# output chain, so the body filter sees buffers before gzip transforms them: the
# preview is therefore readable plaintext, which is what a debugging tool wants
# (a hex dump of deflate output helps nobody). The fact that the client actually
# received gzip is still reported, because content_encoding is read at LOG once
# every filter has run — reading it in our own header filter would always report
# "none", since gzip's header filter runs after ours.
--- http_config
    trace_zone zb12 1m;
--- config
    gzip on;
    gzip_min_length 1;
    gzip_types text/plain;
    location = /b12 {
        trace on;
        trace_body_capture response;
        default_type text/plain;
        return 200 "compress-me-compress-me-compress-me-compress-me";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /b12", "GET /trace/last"]
--- more_headers eval
["Accept-Encoding: gzip", ""]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/"content_encoding"\s*:\s*"gzip".*"preview"\s*:\s*"compress-me-compress-me-compress-me-compress-me"/s]

=== TEST 13: capture stays scoped to the location that enabled it
# An adjacent traced location without trace_body_capture must remain clean, so
# the directive genuinely scopes rather than leaking through the merge.
--- http_config
    trace_zone zb13 1m;
--- config
    location = /b13on {
        trace on;
        trace_body_capture response;
        return 200 "captured-13";
    }
    location = /b13off {
        trace on;
        return 200 "uncaptured-13";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /b13off", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_unlike eval
[qr/(?!)/, qr/"response_body"/]

=== TEST 14: a body preview coexists with the upstream + fault sections
# Guards the slot budget: bodies were added on top of an already-large JSON, so
# assert a transaction carrying upstream data, a fault AND a body still commits
# whole rather than being dropped for exceeding SLOT_MAX.
--- http_config
    trace_zone zb14 1m;
--- config
    location = /b14 {
        trace on;
        trace_body_capture response;
        trace_upstream_capture full;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/bad14;
    }
    location = /bad14 { return 503 "upstream-broke-14"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /b14", "GET /trace/last"]
--- error_code eval
[503, 200]
--- response_body_like eval
[qr//, qr/"upstream".*"fault".*"response_body"/s]

=== TEST 15: G8 — a sendfile'd static response is accounted for, not read back
# A static file reaches the body filter as an in_file buffer with no bytes in
# memory. Reading it back would mean a synchronous disk read on the event loop,
# which a tracer must never do, so the deliberate outcome is: captured_bytes 0,
# an honest total_bytes, and truncated=true to say "there was a body, we did not
# copy it". This documents the trade-off so nobody later "fixes" it into a read.
--- http_config
    trace_zone zb15 1m;
--- config
    location = /b15 {
        trace on;
        trace_body_capture response;
        alias /usr/local/nginx/modules/ngx_http_trace_module.so;
        default_type application/octet-stream;
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /b15", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/"response_body"\s*:\s*\{\s*"captured_bytes"\s*:\s*0\s*,\s*"total_bytes"\s*:\s*[1-9]\d+\s*,\s*"truncated"\s*:\s*true/]

=== TEST 16: FR-BODY-5 — a binary body arriving in memory is emitted as hex
# Same binary file, but proxied: the bytes now arrive through upstream buffers in
# memory, so the sniff sees real NUL/C0 content and must choose preview_hex.
# Asserting the ABSENCE of a plain "preview" key is the point — if the sniff
# regressed, raw NULs would be written into a JSON string literal and the
# document served by the API would be invalid.
--- http_config
    trace_zone zb16 1m;
--- config
    location = /b16 {
        trace on;
        trace_body_capture response;
        trace_body_max 64;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/bin16;
    }
    location = /bin16 {
        alias /usr/local/nginx/modules/ngx_http_trace_module.so;
        default_type application/octet-stream;
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /b16", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/"response_body"[^}]*"preview_hex"\s*:\s*"[0-9a-f]{16,}"/]
--- response_body_unlike eval
[qr/(?!)/, qr/"response_body"[^}]*"preview"\s*:/]

