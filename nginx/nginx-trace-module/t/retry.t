# vi:set ft=perl ts=4 sw=4 et:
#
# M3.3 / FR-RETRY-1 — retries appear as distinct tries in the upstream section.
#
# When proxy_next_upstream retries a failed peer, nginx appends a second entry
# to r->upstream_states. The module harvests each state entry into its own
# tries[] element, so a retried request commits two (or more) tries — the
# failed attempt and the successful one — each with its own status/timing.
#
# We build an upstream group whose first member is a dead port (connection
# refused) and whose second member is the live test server, forcing exactly one
# retry, then read the committed transaction back and count the tries.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();

run_tests();

__DATA__

=== TEST 1: a retried request records two distinct tries
# upstream "retry1" lists a dead peer first (127.0.0.1:1 => connection refused)
# then the live server. proxy_next_upstream error retries onto the live peer,
# so two u->state entries -> two tries[] (seq 0 and seq 1).
--- http_config
    trace_zone zr1 1m;
    upstream retry1 {
        server 127.0.0.1:1        max_fails=0;
        server 127.0.0.1:$TEST_NGINX_SERVER_PORT backup;
    }
--- config
    location = /r1 {
        trace on;
        proxy_next_upstream error timeout invalid_header http_502 http_504;
        proxy_pass http://retry1/r1back;
    }
    location = /r1back { return 200 "r1ok"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /r1", "GET /trace/last"]
--- error_code eval
[200, 200]
# two try objects: at least seq 0 and seq 1 present in the tries array
--- response_body_like eval
["r1ok", qr/"tries"\s*:\s*\[.*"seq"\s*:\s*0.*"seq"\s*:\s*1/s]

=== TEST 2: the successful (final) try carries the 200 status
# The last try is the one that reached the live backend and returned 200; its
# status must be surfaced from the parsed upstream response (M3.3).
--- http_config
    trace_zone zr2 1m;
    upstream retry2 {
        server 127.0.0.1:1        max_fails=0;
        server 127.0.0.1:$TEST_NGINX_SERVER_PORT backup;
    }
--- config
    location = /r2 {
        trace on;
        proxy_next_upstream error timeout invalid_header;
        proxy_pass http://retry2/r2back;
    }
    location = /r2back { return 200 "r2ok"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /r2", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["r2ok", qr/"status"\s*:\s*200/]

=== TEST 3: a single successful upstream produces exactly one try
# No failure => no retry => exactly one try (seq 0) and no seq 1. Guards against
# spurious duplicate try entries in the common (non-retry) path.
--- http_config
    trace_zone zr3 1m;
--- config
    location = /r3 {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/r3back;
    }
    location = /r3back { return 200 "r3ok"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /r3", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["r3ok", qr/"tries"\s*:\s*\[\s*\{[^\[\]]*"seq"\s*:\s*0[^\[\]]*\}\s*\]/s]
--- response_body_unlike eval
[qr/NOMATCH_SENTINEL/, qr/"seq"\s*:\s*1/]

=== TEST 4: each try records its own peer — the dead peer and the live one
# The failed first try must carry the dead peer's address (127.0.0.1:1) and the
# successful try a different (live) peer. Distinct "peer" values across the two
# tries prove retries are harvested as independent attempts, not a duplicated
# single state (FR-RETRY-1 / M3.3 per-attempt model).
--- http_config
    trace_zone zr4 1m;
    upstream retry4 {
        server 127.0.0.1:1        max_fails=0;
        server 127.0.0.1:$TEST_NGINX_SERVER_PORT backup;
    }
--- config
    location = /r4 {
        trace on;
        proxy_next_upstream error timeout invalid_header;
        proxy_pass http://retry4/r4back;
    }
    location = /r4back { return 200 "r4ok"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /r4", "GET /trace/last"]
--- error_code eval
[200, 200]
# seq 0's peer is the dead 127.0.0.1:1, and a later peer differs from it
--- response_body_like eval
["r4ok", qr/"seq"\s*:\s*0,"peer"\s*:\s*"127\.0\.0\.1:1".*"seq"\s*:\s*1,"peer"\s*:\s*"(?!127\.0\.0\.1:1")/s]
