# vi:set ft=perl ts=4 sw=4 et:
#
# M5.6 / AC-12 — cross-worker read path (FR-SHM-4).
#
# The ring buffer and session store live in shared slab memory guarded by the
# zone mutex, so a transaction captured by ANY worker is readable through the
# control endpoint served by ANY worker. With several workers configured, a
# batch of traced requests (distributed across workers by the OS accept loop)
# must all be visible from a single control read.
#
# Test::Nginx hard-codes `worker_processes 1` and `master_process off`; the
# workers()/master_on() helpers override those so a real master + multiple
# workers spawn, genuinely exercising the shared-memory path.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

workers(4);
master_on();

no_shuffle();
run_tests();

__DATA__

=== TEST 1: transaction captured under multi-worker config is read back
--- http_config
    trace_zone zxw1 1m;
--- config
    location = /w { trace on; return 200 "w"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /w", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["w", qr/"uri":"\/w"/]

=== TEST 2: a burst across workers is fully visible from one read
# Several distinct traced URIs; all must appear in the shared ring regardless
# of which worker captured each.
--- http_config
    trace_zone zxw2 1m;
--- config
    location = /m1 { trace on; return 200 "m1"; }
    location = /m2 { trace on; return 200 "m2"; }
    location = /m3 { trace on; return 200 "m3"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /m1", "GET /m2", "GET /m3", "GET /trace/last"]
--- error_code eval
[200, 200, 200, 200]
--- response_body_like eval
["m1", "m2", "m3", qr/"uri":"\/m1".*"uri":"\/m2".*"uri":"\/m3"/]

=== TEST 3 (edge): multi-worker empty ring reads cleanly
--- http_config
    trace_zone zxw3 1m;
--- config
    location = /trace/last { trace_control; }
--- request
GET /trace/last
--- error_code: 200
--- response_body chomp
{"transactions":[]}
