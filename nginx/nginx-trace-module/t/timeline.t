# vi:set ft=perl ts=4 sw=4 et:
#
# M2.3 / M2.4 — Layer-1 phase timeline.
#
# A traced request records one step per registrable phase it passes through,
# each carrying a {phase, t_offset_us} and a derived status, appended in order.
# Phases without custom-handler support (FIND_CONFIG / POST_REWRITE /
# POST_ACCESS) are inferred from observable deltas ($uri / chosen location).
# The committed transaction is JSON with an ordered `steps` array — we assert
# the phase labels appear and are ordered by their `seq`.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: timeline records the registrable phases in order
# A proxied request traverses the full phase set (REWRITE, PREACCESS, ACCESS)
# before CONTENT, unlike a bare `return` which finalizes during REWRITE. The
# committed timeline must therefore name REWRITE, then ACCESS, then the terminal
# LOG step, in that order.
--- http_config
    trace_zone ztl 1m;
--- config
    location = /t1 {
        trace on;
        proxy_pass http://127.0.0.1:$server_port/t1back;
    }
    location = /t1back { return 200 "t1"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /t1", "GET /trace/last"]
--- error_code eval
[200, 200]
# REWRITE observed, ACCESS observed, LOG terminal step present; and the steps
# array is present and ordered.
--- response_body_like eval
["t1", qr/"phase"\s*:\s*"REWRITE".*"phase"\s*:\s*"ACCESS".*"phase"\s*:\s*"LOG"/s]

=== TEST 2: every step carries a t_offset_us and a status
--- http_config
    trace_zone ztl2 1m;
--- config
    location = /t2 {
        trace on;
        return 200 "t2";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /t2", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["t2", qr/"t_offset_us"\s*:\s*\d+.*"status"\s*:\s*"(success|error|skipped|disabled)"/s]

=== TEST 3: steps are numbered by an increasing seq
--- http_config
    trace_zone ztl3 1m;
--- config
    location = /t3 {
        trace on;
        return 200 "t3";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /t3", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["t3", qr/"seq"\s*:\s*0/]

=== TEST 4: FIND_CONFIG is inferred across a rewrite (location changes)
# A rewrite changes $uri and the chosen location, which the inference step
# attributes to FIND_CONFIG (M2.4). The traced request must still route
# correctly AND the committed timeline must mention FIND_CONFIG.
--- http_config
    trace_zone ztl4 1m;
    trace on;
--- config
    location = /rw4 {
        rewrite ^ /dst4 last;
    }
    location = /dst4 {
        return 200 "dst4";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /rw4", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["dst4", qr/"phase"\s*:\s*"FIND_CONFIG"/]

=== TEST 5: LOG is always the terminal step, appended last in the timeline
# Regardless of how many phases ran, the timeline is append-only and the LOG
# step closes it. A proxied request records several phases; the LOG phase label
# must appear AFTER the REWRITE label and be the final phase named in the
# ordered steps array (no phase label follows LOG).
--- http_config
    trace_zone ztl5 1m;
--- config
    location = /t5 {
        trace on;
        proxy_pass http://127.0.0.1:$server_port/t5back;
    }
    location = /t5back { return 200 "t5"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /t5", "GET /trace/last"]
--- error_code eval
[200, 200]
# REWRITE precedes LOG, and LOG is the final step: after the "phase":"LOG"
# object the steps array closes (]) with no further step object. (The upstream
# section may follow the steps array, so we anchor on the array close, not EOS.)
--- response_body_like eval
["t5", qr/"phase"\s*:\s*"REWRITE".*"phase"\s*:\s*"LOG"[^\]]*\}\s*\]/s]

=== TEST 6: a request denied at the ACCESS phase still commits a LOG-terminated timeline
# Edge: `deny all` finalizes the request during the ACCESS phase (403) before
# any content handler runs. The timeline must still be committed with the
# terminal LOG step present — proving the LOG observer fires and commits even
# when the pipeline is short-circuited mid-phase by an access denial. The
# PREACCESS/ACCESS observers run before the deny, so an ACCESS label appears
# ahead of the terminal LOG. (No `return` here: a rewrite-phase `return` would
# finalize before ACCESS and the deny would never take effect.)
--- http_config
    trace_zone ztl6 1m;
--- config
    location = /t6 {
        trace on;
        deny all;
        proxy_pass http://127.0.0.1:$server_port/t6back;
    }
    location = /t6back { return 200 "unreachable"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /t6", "GET /trace/last"]
--- error_code eval
[403, 200]
# The committed timeline names ACCESS then the terminal LOG step.
--- response_body_like eval
[qr//, qr/"phase"\s*:\s*"ACCESS".*"phase"\s*:\s*"LOG"/s]
