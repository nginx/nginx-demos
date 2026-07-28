# vi:set ft=perl ts=4 sw=4 et:
#
# M5.3 — cap enforcement (FR-CFG-9/10, FR-SEL-3).
#
# `trace_max_transactions` caps how many transactions remain visible. With a
# small cap, committing more than the cap keeps only the most-recent `cap`
# entries in the read window — the oldest fall out (bounded, never unbounded).

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: max_transactions caps the visible window to the newest N
# cap = 2; commit 3 → only the two newest (/b, /c) remain; /a evicted.
--- http_config
    trace_zone zcap1 1m;
    trace_max_transactions 2;
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
["a", "b", "c", qr/^\{"transactions":\[\{.*"uri":"\/b".*"uri":"\/c".*\}\]\}$/]
--- response_body_unlike eval
[qr//, qr//, qr//, qr/"uri":"\/a"/]

=== TEST 2: cap of 1 keeps only the single most-recent transaction
--- http_config
    trace_zone zcap2 1m;
    trace_max_transactions 1;
--- config
    location = /x { trace on; return 200 "x"; }
    location = /y { trace on; return 200 "y"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /x", "GET /y", "GET /trace/last"]
--- error_code eval
[200, 200, 200]
--- response_body_like eval
["x", "y", qr/"uri":"\/y"/]
--- response_body_unlike eval
[qr//, qr//, qr/"uri":"\/x"/]

=== TEST 3: below cap, all committed transactions remain visible
--- http_config
    trace_zone zcap3 1m;
    trace_max_transactions 10;
--- config
    location = /p { trace on; return 200 "p"; }
    location = /q { trace on; return 200 "q"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /p", "GET /q", "GET /trace/last"]
--- error_code eval
[200, 200, 200]
--- response_body_like eval
["p", "q", qr/"uri":"\/p".*"uri":"\/q"/]

=== TEST 4 (edge): default cap accepts a burst without truncating a small batch
# No explicit cap → default (200). A 3-request burst is well under it; all show.
--- http_config
    trace_zone zcap4 1m;
--- config
    location = /d1 { trace on; return 200 "d1"; }
    location = /d2 { trace on; return 200 "d2"; }
    location = /d3 { trace on; return 200 "d3"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /d1", "GET /d2", "GET /d3", "GET /trace/last"]
--- error_code eval
[200, 200, 200, 200]
--- response_body_like eval
["d1", "d2", "d3", qr/"uri":"\/d1".*"uri":"\/d2".*"uri":"\/d3"/]
