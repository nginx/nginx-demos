# vi:set ft=perl ts=4 sw=4 et:
#
# M2.5 — watch-list variable snapshot with read/set/set_failed classification.
#
# Only the variables named in the effective `trace_watch` list are evaluated
# (never the full variable set, NFR-PERF-2). Each snapshot records {value, op}:
#   - `read`       : evaluated/read-only so far
#   - `set`        : the variable currently holds a value
#   - `set_failed` : a read-only variable (no set_handler) — an assignment could
#                    not apply (Apigee `≠`, AC-7)
# The committed transaction embeds these under each step's `vars` object.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: watched variable value is captured in the snapshot
--- http_config
    trace_zone zv1 1m;
    trace on;
    trace_watch $request_method;
--- config
    location = /v1 {
        return 200 "v1";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /v1", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["v1", qr/"request_method"\s*:\s*\{\s*"value"\s*:\s*"GET"/]

=== TEST 2: only watched variables appear (uri is NOT watched here)
--- http_config
    trace_zone zv2 1m;
    trace on;
    trace_watch $request_method;
--- config
    location = /v2 {
        return 200 "v2";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /v2", "GET /trace/last"]
--- error_code eval
[200, 200]
# request_method present; the (un-watched) $host must not be snapshotted as a var key.
--- response_body_like eval
["v2", qr/"request_method"/]
--- response_body_unlike eval
[qr//, qr/"host"\s*:\s*\{\s*"value"/]

=== TEST 3: a watched variable carries an op classification
--- http_config
    trace_zone zv3 1m;
    trace on;
    trace_watch $request_method;
--- config
    location = /v3 {
        return 200 "v3";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /v3", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["v3", qr/"op"\s*:\s*"(read|set|set_failed)"/]

=== TEST 4: bare (no leading $) watch name is accepted and snapshotted
--- http_config
    trace_zone zv4 1m;
    trace on;
    trace_watch request_method;
--- config
    location = /v4 {
        return 200 "v4";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /v4", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["v4", qr/"request_method"\s*:\s*\{\s*"value"\s*:\s*"GET"/]

=== TEST 5: multiple watched variables are all snapshotted
# The effective watch list is iterated in full (NFR-PERF-2 scopes it to the
# named set, but every named var is captured). Both $request_method and
# $request_uri must appear with their values in the committed snapshot.
--- http_config
    trace_zone zv5 1m;
    trace on;
    trace_watch $request_method $request_uri;
--- config
    location = /v5 {
        return 200 "v5";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /v5?q=1", "GET /trace/last"]
--- error_code eval
[200, 200]
# Both watched vars must appear in the single committed snapshot JSON.
--- response_body_like eval
["v5", qr/"request_method"\s*:\s*\{\s*"value"\s*:\s*"GET".*"request_uri"\s*:\s*\{\s*"value"\s*:\s*"\/v5\?q=1"/s]

=== TEST 6: a watched variable with no value snapshots as an empty read
# Edge: `$arg_missing` names a query argument that is absent, so the variable
# evaluates not_found. The snapshot must still emit the key with an empty value
# ("") and op "read" — exercising the not_found branch and the empty-string JSON
# path, without crashing or omitting the key.
--- http_config
    trace_zone zv6 1m;
    trace on;
    trace_watch $arg_missing;
--- config
    location = /v6 {
        return 200 "v6";
    }
    location = /trace/last { trace_control; }
--- request eval
["GET /v6", "GET /trace/last"]
--- error_code eval
[200, 200]
# key present with an empty value and a read op; never classified as set.
--- response_body_like eval
["v6", qr/"arg_missing"\s*:\s*\{\s*"value"\s*:\s*""\s*,\s*"op"\s*:\s*"read"\s*\}/]
