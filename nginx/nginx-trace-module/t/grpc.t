# vi:set ft=perl ts=4 sw=4 et:
#
# M3.5 / M3.6 — gRPC protocol classification and trailer-as-truth surfacing.
#
# The upstream section carries a "protocol" field ("http" or "grpc"). The
# module classifies a try as gRPC when the byte-exact captured request advertises
# `content-type: application/grpc` (M3.5). For gRPC the authoritative result is
# the trailer-sourced grpc-status (M3.6, FR-GRPC-2), surfaced distinctly from the
# HTTP :status; for plain HTTP those gRPC fields are absent.
#
# A full HTTP/2 gRPC backend is not available in this harness, so these tests
# pin the classification boundary from the observable side: a plain proxied
# request is classified "http" and carries no grpc_status, and the module never
# misclassifies ordinary traffic as gRPC.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();

run_tests();

__DATA__

=== TEST 1: a plain HTTP proxied request is classified protocol "http"
--- http_config
    trace_zone zg1 1m;
--- config
    location = /g1 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/g1back;
    }
    location = /g1back { return 200 "g1"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /g1", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["g1", qr/"upstream"\s*:\s*\{\s*"protocol"\s*:\s*"http"/]

=== TEST 2: plain HTTP tries carry no gRPC trailer fields
# grpc_status / grpc_message are only emitted for gRPC-classified tries, so an
# ordinary proxied request's try must not contain them (M3.6 boundary).
--- http_config
    trace_zone zg2 1m;
--- config
    location = /g2 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/g2back;
    }
    location = /g2back { return 200 "g2"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /g2", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["g2", qr/"protocol"\s*:\s*"http"/]
--- response_body_unlike eval
[qr/NOMATCH_SENTINEL/, qr/"grpc_status"\s*:/]

=== TEST 3: ordinary traffic is never misclassified as gRPC
# Even with an application/grpc-looking URI, a plain HTTP proxy exchange whose
# bytes do not advertise the gRPC content-type stays "http" — classification is
# byte-driven, not URL-driven.
--- http_config
    trace_zone zg3 1m;
--- config
    location = /application/grpc {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/g3back;
    }
    location = /g3back { return 200 "g3"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /application/grpc", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["g3", qr/"protocol"\s*:\s*"http"/]
--- response_body_unlike eval
[qr/NOMATCH_SENTINEL/, qr/"protocol"\s*:\s*"grpc"/]

=== TEST 4: classification is content-type driven, not HTTP-method driven
# gRPC always rides on POST, but POST alone is not gRPC. A POST proxied upstream
# whose captured bytes do not advertise `content-type: application/grpc` must
# still classify "http" — the module keys on the captured content-type bytes,
# never on the request method (M3.5).
--- http_config
    trace_zone zg4 1m;
--- config
    location = /g4 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/g4back;
    }
    location = /g4back { return 200 "g4"; }
    location = /trace/last { trace_control; }
--- request eval
["POST /g4", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["g4", qr/"protocol"\s*:\s*"http"/]
--- response_body_unlike eval
[qr/NOMATCH_SENTINEL/, qr/"protocol"\s*:\s*"grpc"/]

=== TEST 5: classification stays "http" across a retry (multi-try harvest)
# Edge: prior tests are single-try. A retried request harvests two tries; the
# section-level protocol must remain "http" and NO try may carry grpc_status or
# grpc_message. This proves the harvest path never spuriously promotes a plain
# HTTP exchange to gRPC when multiple state entries are folded in.
--- http_config
    trace_zone zg5 1m;
    upstream grpc5 {
        server 127.0.0.1:1        max_fails=0;
        server 127.0.0.1:$TEST_NGINX_SERVER_PORT backup;
    }
--- config
    location = /g5 {
        trace on;
        proxy_next_upstream error timeout invalid_header;
        proxy_pass http://grpc5/g5back;
    }
    location = /g5back { return 200 "g5"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /g5", "GET /trace/last"]
--- error_code eval
[200, 200]
# protocol http even with two tries; no gRPC trailer fields on any try.
--- response_body_like eval
["g5", qr/"protocol"\s*:\s*"http".*"seq"\s*:\s*0.*"seq"\s*:\s*1/s]
--- response_body_unlike eval
[qr/NOMATCH_SENTINEL/, qr/"grpc_status"\s*:|"grpc_message"\s*:|"protocol"\s*:\s*"grpc"/]
