# M7.2 — trampolines preserve return codes exactly, including the
# NGX_AGAIN / NGX_DONE suspend-resume path used by upstream modules.
# Verifies AC-16: behaviour under intercept is identical to Layer 1.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: a suspended (proxied) request completes normally under intercept
--- http_config
    trace_zone zs1 1m;
    trace_intercept on;
--- config
    location = /s1 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/s1back;
    }
    location = /s1back { return 200 "suspend-ok"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /s1", "GET /trace/last"]
--- response_body_like eval
["suspend-ok", qr/"status":200/]
--- error_code eval
[200, 200]


=== TEST 2: the suspended request yields exactly one CONTENT step
--- http_config
    trace_zone zs2 1m;
    trace_intercept on;
--- config
    location = /s2 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/s2back;
    }
    location = /s2back { return 200 "s2"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /s2", "GET /trace/last"]
--- response_body_like eval
["s2", qr/"phase":"CONTENT"/]
--- response_body_unlike eval
[qr/NOMATCH_SENTINEL/, qr/"phase":"CONTENT".*"phase":"CONTENT"/s]
--- error_code eval
[200, 200]


=== TEST 3: upstream status is preserved through the trampoline
--- http_config
    trace_zone zs3 1m;
    trace_intercept on;
--- config
    location = /s3 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/s3back;
    }
    location = /s3back { return 201 "s3"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /s3", "GET /trace/last"]
--- response_body_like eval
["s3", qr/"status":201/]
--- error_code eval
[201, 200]


=== TEST 4: upstream capture still works alongside the named step
--- http_config
    trace_zone zs4 1m;
    trace_intercept on;
--- config
    location = /s4 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/s4back;
    }
    location = /s4back { return 200 "s4"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /s4", "GET /trace/last"]
--- response_body_like eval
["s4", qr/"upstream":\{.*"tries":\[/s]
--- error_code eval
[200, 200]


=== TEST 5: edge — an errored upstream still returns its code verbatim
--- http_config
    trace_zone zs5 1m;
    trace_intercept on;
--- config
    location = /s5 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/s5back;
    }
    location = /s5back { return 503 "s5"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /s5", "GET /trace/last"]
--- response_body_like eval
["s5", qr/"status":503/]
--- error_code eval
[503, 200]


=== TEST 6: edge — a retried request keeps a single CONTENT step
--- http_config
    trace_zone zs6 1m;
    trace_intercept on;
    upstream s6up {
        server 127.0.0.1:1 max_fails=0;
        server 127.0.0.1:$TEST_NGINX_SERVER_PORT;
    }
--- config
    location = /s6 {
        trace on;
        proxy_next_upstream error timeout;
        proxy_pass http://s6up/s6back;
    }
    location = /s6back { return 200 "s6"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /s6", "GET /trace/last"]
--- response_body_like eval
["s6", qr/"phase":"CONTENT"/]
--- response_body_unlike eval
[qr/NOMATCH_SENTINEL/, qr/"phase":"CONTENT".*"phase":"CONTENT"/s]
--- error_code eval
[200, 200]
