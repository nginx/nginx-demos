# vi:set ft=perl ts=4 sw=4 et:
#
# M2.2 — POST_READ selector + no-trace fast path (G2, FR-SEL-1/2).
#
# The selector runs first in POST_READ, builds the per-request trace context,
# and decides whether the request is traced. A location with `trace on` is
# selected and produces a committed transaction the control endpoint can read
# back; a location without `trace` (or with `trace off`) is NOT selected and
# commits nothing — so the control endpoint still returns the LAST traced
# transaction, never the un-traced one. That difference is the observable proof
# that the no-trace path did zero work.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: a `trace on` location is selected and committed
--- http_config
    trace_zone zsel 1m;
--- config
    location = /on {
        trace on;
        return 200 "on-body";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /on", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["on-body", qr/"uri"\s*:\s*"\/on"/]

=== TEST 2: a location without `trace` is NOT selected (commits nothing)
# First hit a traced location so the shm slot holds a known transaction, then
# hit an un-traced location. The control endpoint must still report the traced
# /seed URI — proving the /notraced request took the no-trace fast path and did
# not overwrite the slot.
--- http_config
    trace_zone zsel2 1m;
--- config
    location = /seed {
        trace on;
        return 200 "seed";
    }
    location = /notraced {
        return 200 "nope";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /seed", "GET /notraced", "GET /trace/last"]
--- error_code eval
[200, 200, 200]
--- response_body_like eval
["seed", "nope", qr/"uri"\s*:\s*"\/seed"/]

=== TEST 3: `trace off` overrides an inherited `trace on` (not selected)
--- http_config
    trace_zone zsel3 1m;
    trace on;
--- config
    location = /seed3 {
        return 200 "seed3";
    }
    location = /off3 {
        trace off;
        return 200 "off3";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /seed3", "GET /off3", "GET /trace/last"]
--- error_code eval
[200, 200, 200]
--- response_body_like eval
["seed3", "off3", qr/"uri"\s*:\s*"\/seed3"/]

=== TEST 4: selection never alters routing (traced rewrite still rewrites)
--- http_config
    trace_zone zsel4 1m;
    trace on;
--- config
    location = /r4 {
        rewrite ^ /rewritten4 last;
    }
    location = /rewritten4 {
        return 200 "rewritten4-ok";
    }
--- request
GET /r4
--- error_code: 200
--- response_body chomp
rewritten4-ok
--- no_error_log
[error]

=== TEST 5: the latest traced request wins (commit overwrites, exactly once)
# Two distinct traced locations are hit in order. The control endpoint reports
# the LAST one (/second5), never /first5 — proving each traced request commits
# exactly one transaction into the ring slot and the newest supersedes.
--- http_config
    trace_zone zsel5 1m;
    trace on;
--- config
    location = /first5 {
        return 200 "first5";
    }
    location = /second5 {
        return 200 "second5";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /first5", "GET /second5", "GET /trace/last"]
--- error_code eval
[200, 200, 200]
--- response_body_like eval
["first5", "second5", qr/"uri"\s*:\s*"\/second5"/]
--- response_body_unlike eval
[qr//, qr//, qr/"uri"\s*:\s*"\/first5"/]

=== TEST 6: server-scoped `trace on` selects a location with no `trace` directive
# Edge: the decision is NOT set in the location block at all — it is inherited
# from the enclosing server scope. Because `decide()` runs only after
# FIND_CONFIG resolves the location, the inherited `enable` must still be seen
# and the request committed. This exercises the inheritance path of the lazy
# decision (distinct from TEST 1, where `trace on` is in the location itself).
--- http_config
    trace_zone zsel6 1m;
    trace on;
--- config
    location = /inherited6 {
        return 200 "inherited6";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /inherited6", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["inherited6", qr/"uri"\s*:\s*"\/inherited6"/]
