# vi:set ft=perl ts=4 sw=4 et:
#
# M6.2/M6.3/M6.4 — routed control-plane API (FR-API-1..13, AC-2).
#
# `trace_control` installed on a *prefix* location routes sub-paths:
#   POST /__trace/sessions                          create   -> 201 TraceSession
#   GET  /__trace/sessions                          list     -> {"sessions":[...]}
#   GET  /__trace/sessions/{id}                     detail   -> TraceSession
#   DEL  /__trace/sessions/{id}                     stop     -> stopped_reason
#   GET  /__trace/sessions/{id}/transactions        summaries
#   GET  /__trace/sessions/{id}/transactions/{txn}  full transaction
#   GET  /__trace/sessions/{id}/export              whole-session artifact
#
# Session creation binds matching traced requests (path filter) to the session,
# which is what lets the list/detail/export tiers return that session's traffic.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: POST /sessions creates a capturing TraceSession (201)
--- http_config
    trace_zone zapi1 1m;
--- config
    location /__trace/ { trace_control; }
--- request
POST /__trace/sessions
--- error_code: 201
--- response_body_like: "id":1,.*"state":"capturing".*"stopped_reason":null

=== TEST 2: GET /sessions lists created sessions
--- http_config
    trace_zone zapi2 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
["POST /__trace/sessions", "GET /__trace/sessions"]
--- error_code eval
[201, 200]
--- response_body_like eval
[qr/"id":1/, qr/"sessions":\[\{"id":1/]

=== TEST 3: GET /sessions/{id} returns that session's detail
--- http_config
    trace_zone zapi3 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
["POST /__trace/sessions", "GET /__trace/sessions/1"]
--- error_code eval
[201, 200]
--- response_body_like eval
[qr/"id":1/, qr/"id":1,.*"state":"capturing"/]

=== TEST 4: GET /sessions/{id} unknown id -> 404
--- http_config
    trace_zone zapi4 1m;
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/sessions/77
--- error_code: 404

=== TEST 5: DELETE /sessions/{id} stops it (stopped_reason=manual)
--- http_config
    trace_zone zapi5 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
["POST /__trace/sessions", "DELETE /__trace/sessions/1"]
--- error_code eval
[201, 200]
--- response_body_like eval
[qr/"id":1/, qr/"state":"stopped".*"stopped_reason":"manual"/]

=== TEST 6: a session bound by path filter captures matching traffic
--- http_config
    trace_zone zapi6 1m;
--- config
    location /__trace/ { trace_control; }
    location = /a6 { trace on; return 200 "a6"; }
--- request eval
["POST /__trace/sessions?path=/a6", "GET /a6", "GET /__trace/sessions/1/transactions"]
--- error_code eval
[201, 200, 200]
--- response_body_like eval
[qr/"id":1/, "a6", qr/"transactions":\[\{"seq":1,"method":"GET","uri":"\/a6"/]

=== TEST 7: transaction detail tier returns the full transaction (steps present)
--- http_config
    trace_zone zapi7 1m;
--- config
    location /__trace/ { trace_control; }
    location = /a7 { trace on; return 200 "a7"; }
--- request eval
["POST /__trace/sessions?path=/a7", "GET /a7", "GET /__trace/sessions/1/transactions/1"]
--- error_code eval
[201, 200, 200]
--- response_body_like eval
[qr/"id":1/, "a7", qr/"txn":"trace".*"steps":\[/]

=== TEST 8: export returns session + its transactions
--- http_config
    trace_zone zapi8 1m;
--- config
    location /__trace/ { trace_control; }
    location = /a8 { trace on; return 200 "a8"; }
--- request eval
["POST /__trace/sessions?path=/a8", "GET /a8", "GET /__trace/sessions/1/export"]
--- error_code eval
[201, 200, 200]
--- response_body_like eval
[qr/"id":1/, "a8", qr/"session":\{"id":1.*"transactions":\[\{"txn":"trace"/]

=== TEST 9 (edge): 429 when trace_max_sessions is reached + stopped_reason surfaces later
--- http_config
    trace_zone zapi9 1m;
    trace_max_sessions 1;
--- config
    location /__trace/ { trace_control; }
--- request eval
["POST /__trace/sessions", "POST /__trace/sessions"]
--- error_code eval
[201, 429]
--- response_body_like eval
[qr/"id":1/, qr/max_sessions_reached/]

=== TEST 10 (edge): unknown txn seq under a real session -> 404
--- http_config
    trace_zone zapi10 1m;
--- config
    location /__trace/ { trace_control; }
    location = /a10 { trace on; return 200 "a10"; }
--- request eval
["POST /__trace/sessions?path=/a10", "GET /a10", "GET /__trace/sessions/1/transactions/999"]
--- error_code eval
[201, 200, 404]

=== TEST 11 (edge): POST to a session sub-path is not allowed (405)
--- http_config
    trace_zone zapi11 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
["POST /__trace/sessions", "POST /__trace/sessions/1/transactions"]
--- error_code eval
[201, 405]

=== TEST 12 (edge): unknown top-level route under the prefix -> 404
--- http_config
    trace_zone zapi12 1m;
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/bogus
--- error_code: 404

=== TEST 13 (edge): legacy exact-match control location still dumps the ring
--- http_config
    trace_zone zapi13 1m;
--- config
    location = /trace/last { trace_control; }
    location = /leg13 { trace on; return 200 "leg"; }
--- request eval
["GET /leg13", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["leg", qr/"transactions":\[.*"uri":"\/leg13"/]

=== TEST 14 (edge): POST /sessions with a fault_only filter reflects it in TraceSession.filter
--- http_config
    trace_zone zapi14 1m;
--- config
    location /__trace/ { trace_control; }
--- request
POST /__trace/sessions?fault_only=1&fault_code=500
--- error_code: 201
--- response_body_like: "filter":\{"path_prefix":"","fault_only":true,"fault_code":500\}

=== TEST 15 (edge): GET /sessions with none created returns an empty array
--- http_config
    trace_zone zapi15 1m;
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/sessions
--- error_code: 200
--- response_body: {"sessions":[],"nsessions":0,"dropped":0}

=== TEST 16 (edge): POST /sessions?max=N clamps and reflects max_transactions
--- http_config
    trace_zone zapi16 1m;
    trace_max_transactions 50;
--- config
    location /__trace/ { trace_control; }
--- request
POST /__trace/sessions?max=3
--- error_code: 201
--- response_body_like: "max_transactions":3,

=== TEST 17 (edge): a faulting transaction surfaces fault:true in the summary tier
--- http_config
    trace_zone zapi17 1m;
--- config
    location /__trace/ { trace_control; }
    location = /boom17 { trace on; return 500 "boom"; }
--- request eval
["POST /__trace/sessions?path=/boom17", "GET /boom17", "GET /__trace/sessions/1/transactions"]
--- error_code eval
[201, 500, 200]
--- response_body_like eval
[qr/"id":1/, qr//, qr/"uri":"\/boom17".*"fault":true/]

=== TEST 18 (edge): export of a session with no captured traffic returns an empty transactions array
--- http_config
    trace_zone zapi18 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
["POST /__trace/sessions?path=/never18", "GET /__trace/sessions/1/export"]
--- error_code eval
[201, 200]
--- response_body_like eval
[qr/"id":1/, qr/"session":\{"id":1.*"transactions":\[\]\}/]
