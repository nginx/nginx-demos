# M7.1/M7.3/M7.4 — Layer 2: per-handler naming for C/dynamic modules.
# Verifies AC-13: the content handler that actually ran is named in the
# timeline when `trace_intercept on`, and stays unnamed under Layer 1.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: intercept off (default) leaves the content handler unnamed
--- http_config
    trace_zone zi1 1m;
--- config
    location = /i1 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/i1back;
    }
    location = /i1back { return 200 "i1"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /i1", "GET /trace/last"]
--- response_body_like eval
["i1", qr/"uri":"\/i1"/]
--- response_body_unlike eval
[qr/NOMATCH_SENTINEL/, qr/"handler":"proxy"/]
--- error_code eval
[200, 200]


=== TEST 2: intercept on names the proxy content handler
--- http_config
    trace_zone zi2 1m;
    trace_intercept on;
--- config
    location = /i2 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/i2back;
    }
    location = /i2back { return 200 "i2"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /i2", "GET /trace/last"]
--- response_body_like eval
["i2", qr/"phase":"CONTENT".*"handler":"proxy"/s]
--- error_code eval
[200, 200]


=== TEST 3: the named CONTENT step carries a duration
--- http_config
    trace_zone zi3 1m;
    trace_intercept on;
--- config
    location = /i3 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/i3back;
    }
    location = /i3back { return 200 "i3"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /i3", "GET /trace/last"]
--- response_body_like eval
["i3", qr/"handler":"proxy".*"duration_us":\d+/s]
--- error_code eval
[200, 200]


=== TEST 4: an untraced request is never named even with intercept on
--- http_config
    trace_zone zi4 1m;
    trace_intercept on;
--- config
    location = /i4 {
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/i4back;
    }
    location = /i4back { return 200 "i4"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /i4", "GET /trace/last"]
--- response_body_like eval
["i4", qr/\{"transactions":\[\]\}/]
--- error_code eval
[200, 200]


=== TEST 5: edge — a rewrite-phase return produces no CONTENT step
--- http_config
    trace_zone zi5 1m;
    trace_intercept on;
--- config
    location = /i5 {
        trace on;
        return 200 "i5";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /i5", "GET /trace/last"]
--- response_body_like eval
["i5", qr/"uri":"\/i5"/]
--- response_body_unlike eval
[qr/NOMATCH_SENTINEL/, qr/"phase":"CONTENT"/]
--- error_code eval
[200, 200]


=== TEST 6: edge — Layer 1 phase steps stay intact under intercept
--- http_config
    trace_zone zi6 1m;
    trace_intercept on;
--- config
    location = /i6 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/i6back;
    }
    location = /i6back { return 200 "i6"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /i6", "GET /trace/last"]
--- response_body_like eval
["i6", qr/"phase":"REWRITE".*"phase":"ACCESS".*"phase":"CONTENT".*"phase":"LOG"/s]
--- error_code eval
[200, 200]
