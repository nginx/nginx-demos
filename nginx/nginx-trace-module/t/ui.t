# vi:set ft=perl ts=4 sw=4 et:
#
# M6.6 — minimal single-page UI (FR-UI-1) + control-plane method/inert gating.
#
# The UI is a self-contained HTML page served at `<prefix>/ui` that talks only
# to the sibling API paths. Here we prove it is served as text/html, that it is
# GET-only, and that the API method gating and inert (no zone) behavior hold.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: GET /ui serves the SPA as text/html
--- http_config
    trace_zone zui1 1m;
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/ui
--- error_code: 200
--- response_headers
Content-Type: text/html
--- response_body_like: <!doctype html>.*nginx trace

=== TEST 2: the UI references the sibling sessions API
--- http_config
    trace_zone zui2 1m;
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/ui
--- response_body_like: /sessions

=== TEST 3: POST /ui is not allowed (405)
--- http_config
    trace_zone zui3 1m;
--- config
    location /__trace/ { trace_control; }
--- request
POST /__trace/ui
--- error_code: 405

=== TEST 4 (edge): DELETE on the sessions collection is not allowed (405)
--- http_config
    trace_zone zui4 1m;
--- config
    location /__trace/ { trace_control; }
--- request
DELETE /__trace/sessions
--- error_code: 405

=== TEST 5 (edge): inert mode (no trace_zone) -> API is unavailable (503)
# With no zone configured the module is inert; the control plane has no store
# to serve and must fail closed rather than crash.
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/sessions
--- error_code: 503

=== TEST 6 (edge): inert mode -> UI is also unavailable (503)
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/ui
--- error_code: 503

=== TEST 7 (edge): HEAD /ui is allowed (GET/HEAD gate), empty body
--- http_config
    trace_zone zui7 1m;
--- config
    location /__trace/ { trace_control; }
--- request
HEAD /__trace/ui
--- error_code: 200
--- response_headers
Content-Type: text/html
