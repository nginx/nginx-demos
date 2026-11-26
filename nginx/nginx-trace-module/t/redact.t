# vi:set ft=perl ts=4 sw=4 et:
#
# M8.0 — redaction (AC-11, G6, NFR-SEC-2/3/8).
#
# The redaction pass is the last thing that touches captured bytes before they
# are serialized into shared memory, so these tests assert on what the trace API
# actually returns: if a secret is visible here, it reached shm.
#
# The two properties that matter and are easy to get wrong:
#   1. a redacted value must be ABSENT, not merely masked somewhere — so every
#      test asserts the plaintext secret does not appear anywhere in the output;
#   2. redaction must be on BY DEFAULT (NFR-SEC-3), so TEST 1 configures no
#      trace_redact at all and still expects Authorization to be destroyed.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: NFR-SEC-3 — Authorization is redacted with NO trace_redact configured
# The default set must apply when the operator never wrote the directive; a
# secure posture you have to remember to switch on is not a secure posture.
--- http_config
    trace_zone zr1 1m;
--- config
    location = /r1 {
        trace on;
        trace_upstream_capture full;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/br1;
    }
    location = /br1 { return 200 "ok"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /r1", "GET /trace/last"]
--- more_headers
Authorization: Bearer supersecrettoken123
--- error_code eval
[200, 200]
--- response_body_unlike eval
[qr/(?!)/, qr/supersecrettoken123/]

=== TEST 2: the Authorization header NAME survives, so the timeline stays readable
# Redaction destroys the value, not the evidence that the header was present.
# An operator debugging auth needs to see that a token was sent.
--- http_config
    trace_zone zr2 1m;
--- config
    location = /r2 {
        trace on;
        trace_upstream_capture full;
        proxy_set_header Authorization "Bearer secretvalue456";
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/br2;
    }
    location = /br2 { return 200 "ok"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /r2", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/Authorization/]
--- response_body_unlike eval
[qr/(?!)/, qr/secretvalue456/]

=== TEST 3: Cookie is in the default set too
--- http_config
    trace_zone zr3 1m;
--- config
    location = /r3 {
        trace on;
        trace_upstream_capture full;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/br3;
    }
    location = /br3 { return 200 "ok"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /r3", "GET /trace/last"]
--- more_headers
Cookie: session=cookiesecret789
--- error_code eval
[200, 200]
--- response_body_unlike eval
[qr/(?!)/, qr/cookiesecret789/]

=== TEST 4: an explicit trace_redact list redacts a custom header
--- http_config
    trace_zone zr4 1m;
--- config
    location = /r4 {
        trace on;
        trace_upstream_capture full;
        trace_redact x-api-key;
        proxy_set_header X-Api-Key "keymaterial000";
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/br4;
    }
    location = /br4 { return 200 "ok"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /r4", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/X-Api-Key/]
--- response_body_unlike eval
[qr/(?!)/, qr/keymaterial000/]

=== TEST 5: matching is case-insensitive (HTTP header names are)
# `trace_redact X-Secret-Hdr` must match a header sent as `x-secret-hdr`.
--- http_config
    trace_zone zr5 1m;
--- config
    location = /r5 {
        trace on;
        trace_upstream_capture full;
        trace_redact X-Secret-Hdr;
        proxy_set_header x-secret-hdr "mixedcasesecret111";
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/br5;
    }
    location = /br5 { return 200 "ok"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /r5", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_unlike eval
[qr/(?!)/, qr/mixedcasesecret111/]

=== TEST 6: an explicit list REPLACES the default set
# This is the sharp edge of the design and is asserted deliberately: once the
# operator writes trace_redact, they own the list. `x-only` is redacted and
# Authorization is not. Documenting this in a test stops someone "fixing" it
# into a merge later without realising it changes configured behavior.
--- http_config
    trace_zone zr6 1m;
--- config
    location = /r6 {
        trace on;
        trace_upstream_capture full;
        trace_redact x-only;
        proxy_set_header X-Only "gone999";
        proxy_set_header Authorization "Bearer kept888";
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/br6;
    }
    location = /br6 { return 200 "ok"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /r6", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/kept888/]
--- response_body_unlike eval
[qr/(?!)/, qr/gone999/]

=== TEST 7: a watched variable with a redacted name is masked, not just headers
# Redaction covers the variable snapshot too (NFR-SEC-2), and there the mask is
# the literal [REDACTED] rather than value-overwriting.
--- http_config
    trace_zone zr7 1m;
--- config
    location = /r7 {
        trace on;
        trace_watch http_authorization;
        trace_redact http_authorization;
        return 200 "ok";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /r7", "GET /trace/last"]
--- more_headers
Authorization: Bearer varsecret777
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/\[REDACTED\]/]
--- response_body_unlike eval
[qr/(?!)/, qr/varsecret777/]

=== TEST 8: the mask does not preserve the secret's length
# A length-preserving mask leaks the token size. Two secrets of very different
# lengths must produce the identical fixed-width mask.
--- http_config
    trace_zone zr8 1m;
--- config
    location = /r8 {
        trace on;
        trace_watch http_x_tok;
        trace_redact http_x_tok;
        return 200 "ok";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /r8", "GET /trace/last"]
--- more_headers
X-Tok: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/"value"\s*:\s*"\[REDACTED\]"/]
--- response_body_unlike eval
[qr/(?!)/, qr/aaaaaaaaaa/]

=== TEST 9: a redacted response header (Set-Cookie) does not reach the ring
# Covers the response direction of the header block, which is a separate capture
# site from the request and so a separate chance to leak.
--- http_config
    trace_zone zr9 1m;
--- config
    location = /r9 {
        trace on;
        trace_upstream_capture full;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/br9;
    }
    location = /br9 {
        add_header Set-Cookie "sid=responsesecret222";
        return 200 "ok";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /r9", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_unlike eval
[qr/(?!)/, qr/responsesecret222/]

=== TEST 10: redaction does not corrupt the surrounding JSON
# A masking bug that ran past the value would break the header block and the
# enclosing JSON. Assert the document still parses as a trace transaction.
--- http_config
    trace_zone zr10 1m;
--- config
    location = /r10 {
        trace on;
        trace_upstream_capture full;
        proxy_set_header Authorization "Bearer structsecret333";
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/br10;
    }
    location = /br10 { return 200 "ok"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /r10", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/"txn"\s*:\s*"trace".*"steps"\s*:\s*\[.*\].*"upstream"/s]
--- response_body_unlike eval
[qr/(?!)/, qr/structsecret333/]

=== TEST 11: the response itself is unchanged by redaction (G1 transparency)
# Redaction rewrites our pool copies, never nginx's own buffers. The client must
# still receive the real Set-Cookie header.
--- http_config
    trace_zone zr11 1m;
--- config
    location = /r11 {
        trace on;
        trace_upstream_capture full;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/br11;
    }
    location = /br11 {
        add_header Set-Cookie "sid=clientmustsee444";
        return 200 "body-intact";
    }
--- request
GET /r11
--- error_code: 200
--- response_body chomp
body-intact
--- response_headers
Set-Cookie: sid=clientmustsee444
