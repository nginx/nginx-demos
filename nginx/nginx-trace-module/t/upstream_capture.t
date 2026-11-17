# vi:set ft=perl ts=4 sw=4 et:
#
# M0.3 (spike) — byte-exact upstream request/response capture.
#
# The module wraps r->upstream->create_request and ->process_header on a
# proxy_pass request, then logs the exact bytes sent to / received from the
# upstream with stable markers:
#
#   ngx-trace: upstream-request-bytes >>>...<<<
#   ngx-trace: upstream-response-bytes >>>...<<<
#
# The test drives a proxied request and asserts both markers appear in the
# error log and that the captured request line / response status line match the
# real bytes (FR-UP-2/3, skill:upstream-create-request / upstream-process-header).
#
# NOTE: M8.6 demoted this emit from NGX_LOG_NOTICE to the debug log (NFR-SEC-7).
# Raw payload bytes in the error_log sit outside the M8.0 redaction pass and land
# in a file with different permissions than the trace API, so a default
# production error_log must never receive them. The spike's capture behavior is
# unchanged and still asserted below — only the severity moved, so this suite now
# runs at `debug`. `trace_hardened on` suppresses the emit entirely; that case is
# covered in t/hardened.t.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

log_level('debug');
no_shuffle();

run_tests();

__DATA__

=== TEST 1: capture markers appear for a proxied request
--- config
    location = /front {
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/back;
    }
    location = /back {
        return 200 "backend-body";
    }
--- request
GET /front
--- response_body chomp
backend-body
--- error_code: 200
--- grep_error_log eval
qr/ngx-trace: upstream-(request|response)-bytes/
--- grep_error_log_out
ngx-trace: upstream-request-bytes
ngx-trace: upstream-response-bytes

=== TEST 2: captured request bytes contain the exact request line sent upstream
--- config
    location = /front2 {
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/back2;
    }
    location = /back2 {
        return 200 "ok2";
    }
--- request
GET /front2
--- error_code: 200
--- error_log
GET /back2 HTTP/1.0

=== TEST 3: captured response bytes contain the exact status line received
--- config
    location = /front3 {
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/back3;
    }
    location = /back3 {
        return 201 "created3";
    }
--- request
GET /front3
--- error_code: 201
--- error_log
HTTP/1.1 201

=== TEST 4: captured request bytes contain the exact Host header sent upstream
# Proves header-level capture (not just the request line): proxy_pass sets the
# upstream Host header to the proxied host:port, which must appear verbatim in
# the captured request bytes.
--- config
    location = /front4 {
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/back4;
    }
    location = /back4 {
        return 200 "ok4";
    }
--- request
GET /front4
--- error_code: 200
--- error_log eval
qr{Host: 127\.0\.0\.1:\d+}
