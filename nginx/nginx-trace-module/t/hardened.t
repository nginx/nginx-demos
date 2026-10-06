# vi:set ft=perl ts=4 sw=4 et:
#
# M8.6 — hardened mode (NFR-SEC-7).
#
# `trace_hardened on` is the global kill switch for deployments where payload
# bytes must never be captured, no matter what any location says. It exists
# because body capture is a per-location directive: without a global override, a
# single `trace_body_capture both` in one included config file would defeat the
# deployment's security posture, and nobody would notice.
#
# Two guarantees are asserted:
#   1. body capture is force-disabled even where explicitly configured — the
#      global switch WINS over the local directive, not the other way round;
#   2. the raw-byte error_log emit inherited from the M0 spike is suppressed
#      entirely, since those bytes bypass the M8.0 redaction pass.
#
# Everything else about tracing must keep working: hardened mode removes payload
# capture, not observability.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

log_level('debug');
no_shuffle();
run_tests();

__DATA__

=== TEST 1: hardened mode overrides an explicit trace_body_capture
# The location asks for body capture; the global switch must refuse it.
--- http_config
    trace_zone zh1 1m;
    trace_hardened on;
--- config
    location = /h1 {
        trace on;
        trace_body_capture both;
        return 200 "must-not-be-captured-h1";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /h1", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_unlike eval
[qr/(?!)/, qr/must-not-be-captured-h1|"response_body"/]

=== TEST 2: the same config WITHOUT hardened does capture
# The control case. Without this, TEST 1 would pass even if body capture were
# broken outright, and would prove nothing about hardened mode.
--- http_config
    trace_zone zh2 1m;
--- config
    location = /h2 {
        trace on;
        trace_body_capture both;
        return 200 "captured-h2";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /h2", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/"response_body"[^}]*"preview"\s*:\s*"captured-h2"/]

=== TEST 3: hardened mode suppresses the raw upstream-bytes debug emit
# Those bytes are outside the redaction pass and land in the error_log, so under
# hardened mode they must not be written even at debug level.
--- http_config
    trace_zone zh3 1m;
    trace_hardened on;
--- config
    location = /h3 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/bh3;
    }
    location = /bh3 { return 200 "ok3"; }
--- request
GET /h3
--- error_code: 200
--- no_error_log
ngx-trace: upstream-request-bytes

=== TEST 4: hardened mode still traces — the timeline is intact
# Hardened removes payload capture, not observability. The transaction must still
# commit with its phase timeline.
--- http_config
    trace_zone zh4 1m;
    trace_hardened on;
--- config
    location = /h4 {
        trace on;
        return 200 "ok4";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /h4", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/"txn"\s*:\s*"trace".*"steps"\s*:\s*\[.*"phase"\s*:\s*"LOG"/s]

=== TEST 5: hardened mode keeps upstream header capture (metadata, redacted)
# Header capture is metadata and stays available — it goes through the redaction
# pass, unlike the error_log emit. Hardened must not disable it.
--- http_config
    trace_zone zh5 1m;
    trace_hardened on;
--- config
    location = /h5 {
        trace on;
        trace_upstream_capture full;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/bh5;
    }
    location = /bh5 { return 200 "ok5"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /h5", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
[qr//, qr/"upstream".*"tries".*"request"/s]

=== TEST 6: trace_hardened off behaves as the default (emit present at debug)
# Proves the flag is genuinely wired to the switch rather than the emit having
# been removed unconditionally.
--- http_config
    trace_zone zh6 1m;
    trace_hardened off;
--- config
    location = /h6 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/bh6;
    }
    location = /bh6 { return 200 "ok6"; }
--- request
GET /h6
--- error_code: 200
--- error_log
ngx-trace: upstream-request-bytes

=== TEST 7: trace_hardened is rejected outside the main context
# A security posture that could be relaxed per-location would be worthless, so
# the directive is main-context only and nginx must refuse to start otherwise.
--- http_config
    trace_zone zh7 1m;
--- config
    location = /h7 {
        trace_hardened on;
        return 200 "nope";
    }
--- must_die
--- error_log_like
qr/"trace_hardened" directive is not allowed here/
