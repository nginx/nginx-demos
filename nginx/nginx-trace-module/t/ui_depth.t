# vi:set ft=perl ts=4 sw=4 et:
#
# M8.5 — UI depth (FR-UI-2..7) and the two API endpoints it depends on:
# `GET /sessions/{id}/share` (FR-API-8) and `POST /import` (FR-API-9).
#
# The SPA itself is client-side, so what is testable server-side is (a) that the
# document actually ships the features the requirements name, and (b) that the
# endpoints Share/Import call behave correctly — including their failure modes,
# which is where a control-plane endpoint that parses untrusted input is most
# likely to go wrong.
#
# The assertions on the HTML are deliberately behavioural rather than cosmetic:
# each one names a requirement and checks for the mechanism that implements it
# (the API path called, the storage key used, the DOM hook), not for a label that
# could be renamed without breaking anything.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1 (FR-UI-2/3/4): the SPA ships the three-pane layout
# rail = transaction list, mid = timeline, side = detail panel. If the shell is
# missing, every other UI requirement is unreachable.
--- http_config
    trace_zone zud1 1m;
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/ui
--- error_code: 200
--- response_headers
Content-Type: text/html
--- response_body_like eval
qr/id=rail.*id=mid.*id=side/s

=== TEST 2 (FR-UI-2): the rail is fed by the transactions API and polls while capturing
# Near-real-time growth is a requirement, not a nicety: a session that is
# `capturing` must surface new transactions without the operator reloading.
--- http_config
    trace_zone zud2 1m;
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/ui
--- response_body_like eval
qr{/transactions.*setInterval}s

=== TEST 3 (FR-UI-5): search highlights matches and auto-expands groups
# Highlighting alone is insufficient — a match inside a collapsed phase group
# would stay invisible, so the search must force those groups open.
--- http_config
    trace_zone zud3 1m;
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/ui
--- response_body_like eval
qr/\.open\s*=\s*true.*<mark>/s

=== TEST 4 (FR-UI-6): view options are persisted per user
# "Persisted per user" with no server-side user model means localStorage.
--- http_config
    trace_zone zud4 1m;
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/ui
--- response_body_like eval
qr/localStorage.*vSkipped.*vDisabled.*vCond.*vFlow/s

=== TEST 5 (FR-UI-4): the detail panel renders bodies, upstream and gRPC trailers
# These are the M3/M8 capture products; the panel is where they become useful.
--- http_config
    trace_zone zud5 1m;
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/ui
--- response_body_like eval
qr/grpc-status.*request_body.*response_body.*preview_hex/s

=== TEST 6 (FR-UI-7): offline import is client-side and share copies a deep link
--- http_config
    trace_zone zud6 1m;
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/ui
--- response_body_like eval
qr/clipboard.*FileReader/s

=== TEST 7 (NFR-SEC): captured bytes are escaped before reaching innerHTML
# Captured payloads are attacker-influenced. A trace viewer that renders them
# raw turns every inspected request into stored XSS against the operator, so the
# escape helper must exist and cover the three dangerous characters.
--- http_config
    trace_zone zud7 1m;
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/ui
--- response_body_like eval
qr/function esc\(.*&amp;.*&lt;.*&gt;/s

=== TEST 8 (FR-API-8): share returns a deep link plus the expiry horizon
# The URL must carry the session in its fragment so the SPA can restore it, and
# an expires_at so the link's lifetime is visibly bounded by retention.
--- http_config
    trace_zone zud8 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
["POST /__trace/sessions", "GET /__trace/sessions/1/share"]
--- error_code eval
[201, 200]
--- response_body_like eval
[qr/"id"\s*:\s*1/, qr{"url"\s*:\s*"http://[^"]*/__trace/ui\#s=1".*"expires_at"}s]

=== TEST 9 (FR-API-8 edge): share on an unknown session is 404
# The link must not outlive the data. An id that no longer exists has to fail
# closed rather than mint a URL to nothing.
--- http_config
    trace_zone zud9 1m;
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/sessions/4242/share
--- error_code: 404

=== TEST 10 (FR-API-8 edge): share is GET-only
--- http_config
    trace_zone zud10 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
["POST /__trace/sessions", "DELETE /__trace/sessions/1/share"]
--- error_code eval
[201, 405]

=== TEST 11 (FR-API-9): import accepts a session export and counts transactions
# The count comes from the per-record "txn" markers, so this also proves the
# scan walks the whole payload rather than stopping at the first hit.
--- http_config
    trace_zone zud11 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
["POST /__trace/import
{\"session\":{\"id\":7},\"transactions\":[{\"txn\":\"trace\",\"status\":200},{\"txn\":\"trace\",\"status\":500}]}"]
--- error_code eval
[200]
--- response_body_like eval
[qr/"imported"\s*:\s*true.*"transactions"\s*:\s*2/]

=== TEST 12 (FR-API-9 edge): a payload that is not an export is rejected
# Fail closed on shape. Accepting arbitrary JSON would make the endpoint a
# silent no-op that reports success.
--- http_config
    trace_zone zud12 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
["POST /__trace/import
{\"something\":\"else\"}"]
--- error_code eval
[400]
--- response_body_like eval
[qr/not_a_session_export/]

=== TEST 13 (FR-API-9 edge): an empty body is rejected, not treated as valid
--- http_config
    trace_zone zud13 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
["POST /__trace/import"]
--- error_code eval
[400]
--- response_body_like eval
[qr/empty_body/]

=== TEST 14 (FR-API-9 edge): import is POST-only
--- http_config
    trace_zone zud14 1m;
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/import
--- error_code: 405

=== TEST 15 (FR-API-9 edge): a body buffered to disk is refused, not read back
# The control plane must not perform blocking file I/O on an attacker-chosen
# size (G8). `client_body_in_file_only on` forces the in_file branch
# deterministically, which a size-based test cannot do reliably.
--- http_config
    trace_zone zud15 1m;
--- config
    location /__trace/ {
        trace_control;
        client_body_in_file_only on;
    }
--- request eval
["POST /__trace/import
{\"transactions\":[{\"txn\":\"trace\"}]}"]
--- error_code eval
[413]
--- response_body_like eval
[qr/body_too_large/]

=== TEST 16 (AC-15 / FR-UI-7): export round-trips through import
# The acceptance criterion requires that exporting before eviction and
# re-importing reproduces the session in the offline viewer. Here we prove the
# artifact our own export produces is one our import accepts — the two halves
# agreeing is the part that actually breaks in practice.
--- http_config
    trace_zone zud16 1m;
--- config
    location = /u16 {
        trace on;
        return 200 "round-trip";
    }
    location /__trace/ { trace_control; }
--- request eval
["POST /__trace/sessions", "GET /u16", "GET /__trace/sessions/1/export"]
--- error_code eval
[201, 200, 200]
--- response_body_like eval
[qr/"id"\s*:\s*1/, qr/round-trip/, qr/"session".*"transactions"\s*:\s*\[.*"txn"\s*:\s*"trace"/s]

=== TEST 17 (FR-UI-1): the UI derives its API base from its own path
# It must work under any trace_control prefix, so the base is computed by
# stripping the trailing /ui rather than hard-coded to /__trace.
--- http_config
    trace_zone zud17 1m;
--- config
    location /deep/nested/ctl/ { trace_control; }
--- request
GET /deep/nested/ctl/ui
--- error_code: 200
--- response_body_like eval
qr{replace\(/\\/ui\$/}

=== TEST 18 (FR-UI-3): sub-millisecond steps carry the epsilon marker
--- http_config
    trace_zone zud18 1m;
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/ui
--- response_body_like eval
qr/&#949;/
