# vi:set ft=perl ts=4 sw=4 et:
#
# M4.3 — fault-only sessions (FR-SEL-4, AC-10).
#
# With `trace_fault_only on`, a traced request is recorded provisionally in the
# request-pool ctx and committed to the ring buffer at LOG ONLY if it finalized
# as a fault; successful requests are discarded and the ring buffer is left
# untouched. An optional code narrows the filter to an exact finalizing status.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: AC-10 — a success under fault_only is discarded (ring unchanged)
# First commit a fault so the ring has a known last entry, then a success under
# fault_only must NOT overwrite it: /trace/last still shows the earlier fault.
--- http_config
    trace_zone zfo1 1m;
--- config
    location = /fail1 {
        trace on;
        trace_fault_only on;
        return 500;
    }
    location = /ok1 {
        trace on;
        trace_fault_only on;
        return 200 "ok1";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /fail1", "GET /ok1", "GET /trace/last"]
--- error_code eval
[500, 200, 200]
--- response_body_like eval
[qr//, "ok1", qr/"uri"\s*:\s*"\/fail1".*"status"\s*:\s*500/s]

=== TEST 2: a fault under fault_only IS committed
--- http_config
    trace_zone zfo2 1m;
--- config
    location = /fail2 {
        trace on;
        trace_fault_only on;
        return 503;
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /fail2", "GET /trace/last"]
--- error_code eval
[503, 200]
--- response_body_like eval
[qr//, qr/"uri"\s*:\s*"\/fail2".*"status"\s*:\s*503/s]

=== TEST 3: fault_only with a specific code commits only that code
# A 500 under `fault_only on 502` must be discarded; the earlier 502 stays.
--- http_config
    trace_zone zfo3 1m;
--- config
    location = /match3 {
        trace on;
        trace_fault_only on 502;
        return 502;
    }
    location = /other3 {
        trace on;
        trace_fault_only on 502;
        return 500;
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /match3", "GET /other3", "GET /trace/last"]
--- error_code eval
[502, 500, 200]
--- response_body_like eval
[qr//, qr//, qr/"uri"\s*:\s*"\/match3".*"status"\s*:\s*502/s]

=== TEST 4: fault_only off behaves normally (successes committed)
--- http_config
    trace_zone zfo4 1m;
--- config
    location = /ok4 {
        trace on;
        trace_fault_only off;
        return 200 "ok4";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /ok4", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["ok4", qr/"uri"\s*:\s*"\/ok4".*"status"\s*:\s*200/s]

=== TEST 5: a lone success under fault_only leaves the ring empty
# Nothing has ever committed to this fresh zone; a discarded success must not
# create a transaction, so the control endpoint reports an empty result.
--- http_config
    trace_zone zfo5 1m;
--- config
    location = /ok5 {
        trace on;
        trace_fault_only on;
        return 200 "ok5";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /ok5", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["ok5", qr/\{"transactions":\[\]\}/]

=== TEST 6: fault_only with a code commits when the code matches exactly
--- http_config
    trace_zone zfo6 1m;
--- config
    location = /match6 {
        trace on;
        trace_fault_only on 404;
        return 404;
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /match6", "GET /trace/last"]
--- error_code eval
[404, 200]
--- response_body_like eval
[qr//, qr/"uri"\s*:\s*"\/match6".*"status"\s*:\s*404/s]

=== TEST 7: M4.3 — server-scoped trace_fault_only is inherited by locations
# trace_fault_only set at server scope must apply to a location that declares
# none of its own: the success is discarded (ring empty) while the fault under
# the same inherited setting commits. Proves merge/inheritance of the filter.
--- http_config
    trace_zone zfo7 1m;
--- config
    trace on;
    trace_fault_only on;
    location = /okg7 {
        return 200 "okg7";
    }
    location = /failg7 {
        return 500;
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /okg7", "GET /failg7", "GET /trace/last"]
--- error_code eval
[200, 500, 200]
--- response_body_like eval
["okg7", qr//, qr/"uri"\s*:\s*"\/failg7".*"status"\s*:\s*500/s]
