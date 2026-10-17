# vi:set ft=perl ts=4 sw=4 et:
#
# M2.6 — step status derivation.
#
# The terminal step's status is derived from the finalizing HTTP status: a
# 4xx/5xx makes it `error`, everything else stays `success`. Full per-handler
# fault attribution lands in M4; this milestone proves the derivation hook and
# the schema label. Status is read back from the committed transaction JSON.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: a 2xx response yields a success terminal step
--- http_config
    trace_zone zst1 1m;
--- config
    location = /ok1 {
        trace on;
        return 200 "ok1";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /ok1", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["ok1", qr/"status"\s*:\s*"success"/]

=== TEST 2: a 5xx response yields an error terminal step
--- http_config
    trace_zone zst2 1m;
--- config
    location = /boom2 {
        trace on;
        return 500;
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /boom2", "GET /trace/last"]
--- error_code eval
[500, 200]
--- response_body_like eval
[qr//, qr/"status"\s*:\s*"error"/]

=== TEST 3: a 4xx response yields an error terminal step
--- http_config
    trace_zone zst3 1m;
--- config
    location = /forbidden3 {
        trace on;
        return 403;
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /forbidden3", "GET /trace/last"]
--- error_code eval
[403, 200]
--- response_body_like eval
[qr//, qr/"status"\s*:\s*"error"/]

=== TEST 4: the transaction top-level status mirrors the HTTP status
--- http_config
    trace_zone zst4 1m;
--- config
    location = /code4 {
        trace on;
        return 201 "created4";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /code4", "GET /trace/last"]
--- error_code eval
[201, 200]
--- response_body_like eval
["created4", qr/"status"\s*:\s*201/]

=== TEST 5: a 3xx redirect is NOT an error (terminal step stays success)
# The error derivation boundary is 4xx/5xx only. A 302 redirect must keep the
# terminal step `status:"success"` while the transaction top-level status
# mirrors 302 — proving 3xx does not trip the error branch.
--- http_config
    trace_zone zst5 1m;
--- config
    location = /redir5 {
        trace on;
        return 302 "http://example.test/elsewhere";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /redir5", "GET /trace/last"]
--- error_code eval
[302, 200]
# Top-level status mirrors 302 AND the terminal step stayed success (no error).
--- response_body_like eval
[qr//, qr/"status"\s*:\s*302.*"status"\s*:\s*"success"/s]
--- response_body_unlike eval
[qr/NOMATCH_SENTINEL/, qr/"status"\s*:\s*"error"/]

=== TEST 6: an nginx-originated 404 (no matching resource) derives an error step
# Edge: unlike TEST 2/3 which force a code via `return`, here nginx itself
# generates the 404 (a static location with no file to serve). The status the
# derivation reads comes through the err_status fallback path, and the terminal
# step must still be classified `error` with the top-level status mirroring 404.
--- http_config
    trace_zone zst6 1m;
--- config
    location = /missing6 {
        trace on;
        # A static root pointing at a directory with no such file => nginx 404.
        root /tmp/ngx-trace-nonexistent-dir;
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /missing6", "GET /trace/last"]
--- error_code eval
[404, 200]
# top-level status mirrors 404 and the terminal step is an error.
--- response_body_like eval
[qr//, qr/"status"\s*:\s*404.*"status"\s*:\s*"error"/s]
