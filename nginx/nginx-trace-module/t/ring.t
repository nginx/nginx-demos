# vi:set ft=perl ts=4 sw=4 et:
#
# M5.2 — bounded transaction ring buffer (FR-SHM-1, NFR-PERF-4).
#
# The control endpoint now returns ALL live transactions as
# {"transactions":[...]}, oldest-first. Committing several traced requests
# accumulates them in the ring; the ring is bounded, so once capacity is
# exceeded the oldest entries fall out of the read window (never unbounded
# growth). Commit is still exactly-once per request.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: multiple traced requests accumulate in the ring (oldest-first)
--- http_config
    trace_zone zring1 1m;
--- config
    location = /a { trace on; return 200 "a"; }
    location = /b { trace on; return 200 "b"; }
    location = /c { trace on; return 200 "c"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /a", "GET /b", "GET /c", "GET /trace/last"]
--- error_code eval
[200, 200, 200, 200]
--- response_body_like eval
["a", "b", "c", qr/"uri":"\/a".*"uri":"\/b".*"uri":"\/c"/]

=== TEST 2: response is a well-formed {"transactions":[...]} envelope
--- http_config
    trace_zone zring2 1m;
--- config
    location = /one { trace on; return 200 "one"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /one", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["one", qr/^\{"transactions":\[\{.*\}\]\}$/]

=== TEST 3: empty ring returns an empty transactions array (not 404)
--- http_config
    trace_zone zring3 1m;
--- config
    location = /trace/last { trace_control; }
--- request
GET /trace/last
--- error_code: 200
--- response_body chomp
{"transactions":[]}

=== TEST 4: exactly-once — one request yields exactly one ring entry
# Two "uri" keys would mean a double-commit. A single /solo hit must appear once.
--- http_config
    trace_zone zring4 1m;
--- config
    location = /solo { trace on; return 200 "solo"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /solo", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["solo", qr/"uri":"\/solo"/]
--- response_body_unlike eval
[qr//, qr/"txn":"trace".*"txn":"trace"/s]

=== TEST 5 (edge): un-traced requests never enter the ring
# Interleave traced and un-traced hits; only the traced ones appear, in order.
--- http_config
    trace_zone zring5 1m;
--- config
    location = /t1 { trace on;  return 200 "t1"; }
    location = /u1 { trace off; return 200 "u1"; }
    location = /t2 { trace on;  return 200 "t2"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /t1", "GET /u1", "GET /t2", "GET /trace/last"]
--- error_code eval
[200, 200, 200, 200]
--- response_body_like eval
["t1", "u1", "t2", qr/^\{"transactions":\[\{.*"uri":"\/t1".*"uri":"\/t2".*\}\]\}$/]
--- response_body_unlike eval
[qr//, qr//, qr//, qr/"uri":"\/u1"/]
