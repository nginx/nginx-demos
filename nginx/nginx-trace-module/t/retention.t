# vi:set ft=perl ts=4 sw=4 et:
#
# M5.4 — retention / session lookup 404 (NFR-PERF-5, AC-15, FR-API-13).
#
# The control endpoint accepts an optional `?session=<id>` filter. An unknown
# or fully-evicted session id yields 404 (the same status a post-retention
# expired session reaches once its grace horizon passes). Session creation is
# the M6 API surface; here we prove the lookup/404 contract and that the
# unfiltered ring read is unaffected.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: unknown session id returns 404
--- http_config
    trace_zone zret1 1m;
--- config
    location = /seed { trace on; return 200 "seed"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /seed", "GET /trace/last?session=999"]
--- error_code eval
[200, 404]

=== TEST 2: session=0 is not a valid public id (404)
--- http_config
    trace_zone zret2 1m;
--- config
    location = /trace/last { trace_control; }
--- request
GET /trace/last?session=0
--- error_code: 404

=== TEST 3: non-numeric session id is rejected (404)
--- http_config
    trace_zone zret3 1m;
--- config
    location = /trace/last { trace_control; }
--- request
GET /trace/last?session=abc
--- error_code: 404

=== TEST 4: unfiltered read still works alongside the session filter
--- http_config
    trace_zone zret4 1m;
--- config
    location = /keep { trace on; return 200 "keep"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /keep", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["keep", qr/"uri":"\/keep"/]

=== TEST 5 (edge): short retention configured — ring read remains available
# A tiny trace_retention must not break the normal (session-less) ring read;
# retention only governs session lifecycle, and ring transactions stay readable.
--- http_config
    trace_zone zret5 1m;
    trace_retention 1s;
--- config
    location = /rr { trace on; return 200 "rr"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /rr", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["rr", qr/"uri":"\/rr"/]
