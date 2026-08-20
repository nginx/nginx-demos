# vi:set ft=perl ts=4 sw=4 et:
#
# M0.3 / FR-LOG-6 — upstream capture must not leak raw bytes to the error log.
#
# These requests proxy upstream under debug logging and assert that the module
# never writes raw upstream bytes into nginx's error log — neither for traced
# requests nor for `trace off` traffic carrying sensitive headers.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

log_level('debug');
no_shuffle();

run_tests();

__DATA__

=== TEST 1: a traced proxied request emits no raw upstream byte markers
--- http_config
    trace_zone zcap1 1m;
--- config
    location = /front {
        trace on;
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
--- no_error_log
ngx-trace: upstream-request-bytes
ngx-trace: upstream-response-bytes

=== TEST 2: a traced proxied request does not log the raw upstream request line
--- http_config
    trace_zone zcap2 1m;
--- config
    location = /front2 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/back2;
    }
    location = /back2 {
        return 200 "ok2";
    }
--- request
GET /front2
--- error_code: 200
--- no_error_log
GET /back2 HTTP/1.0

=== TEST 3: a traced proxied request does not log the raw upstream status line
--- http_config
    trace_zone zcap3 1m;
--- config
    location = /front3 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/back3;
    }
    location = /back3 {
        return 201 "created3";
    }
--- request
GET /front3
--- error_code: 201
--- no_error_log
HTTP/1.1 201

=== TEST 4: a traced proxied request does not log the raw upstream Host header
--- http_config
    trace_zone zcap4 1m;
--- config
    location = /front4 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/back4;
    }
    location = /back4 {
        return 200 "ok4";
    }
--- request
GET /front4
--- error_code: 200
--- no_error_log eval
qr{Host: 127\.0\.0\.1:\d+}

=== TEST 5: a trace-off proxied request does not leak sensitive headers to the error log
--- http_config
    trace_zone zcap5 1m;
--- config
    location = /front5 {
        trace off;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/back5;
    }
    location = /back5 {
        return 200 "ok5";
    }
--- more_headers
Authorization: top-secret-auth
Cookie: sid=top-secret-cookie
--- request
GET /front5
--- error_code: 200
--- no_error_log
top-secret-auth
sid=top-secret-cookie
ngx-trace: upstream-request-bytes
