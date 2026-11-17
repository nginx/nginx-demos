# M9 — Layer-3 emit API & divergence fixes (D2, D14, D22, D24, M9.3)

use lib 'inc';
use Test::Nginx::Socket 'no_plan';

no_shuffle();
run_tests();

__DATA__

=== TEST 1: D2 — session created with no max= arg defaults to 64, not 200
--- http_config
    trace_zone ze1 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
["GET /__trace/session?path=/", "GET /__trace/sessions"]
--- response_body_like eval
[qr/"id":/, qr/"max_transactions":64/]
--- error_code eval
[200, 200]

=== TEST 2: D2 — max=128 clamps to ring capacity (64)
--- http_config
    trace_zone ze2 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
["GET /__trace/session?path=/&max=128", "GET /__trace/sessions"]
--- response_body_like eval
[qr/"id":/, qr/"max_transactions":64/]
--- error_code eval
[200, 200]

=== TEST 3: D2 — explicit max=32 under the ring limit is respected
--- http_config
    trace_zone ze3 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
["GET /__trace/session?path=/&max=32", "GET /__trace/sessions"]
--- response_body_like eval
[qr/"id":/, qr/"max_transactions":32/]
--- error_code eval
[200, 200]

=== TEST 4: D14 — transaction JSON carries worker_pid
--- http_config
    trace_zone ze4 1m;
--- config
    location /trace {
        trace on;
        return 200 "pid test";
    }
    location /__trace/ { trace_control; }
--- request eval
["GET /trace", "GET /__trace/last"]
--- response_body_like eval
["pid test", qr/"worker_pid":[1-9]\d*/]
--- response_body_unlike eval
[qr/(?!)/, qr/"worker_pid":0/]
--- error_code eval
[200, 200]

=== TEST 5: D14 — connection_id is a non-zero per-connection integer
--- http_config
    trace_zone ze5 1m;
--- config
    location /trace {
        trace on;
        return 200 "conn test";
    }
    location /__trace/ { trace_control; }
--- request eval
["GET /trace", "GET /__trace/last"]
--- response_body_like eval
["conn test", qr/"connection_id":[1-9]\d*/]
--- error_code eval
[200, 200]

=== TEST 6: D22 — ttl=30 creates a session (expires_at present)
--- http_config
    trace_zone ze6 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
["GET /__trace/session?path=/&max=10&ttl=30", "GET /__trace/sessions"]
--- response_body_like eval
[qr/"id":/, qr/"expires_at":\d+/]
--- error_code eval
[200, 200]

=== TEST 7: D22 — no ttl arg falls back to trace_retention
--- http_config
    trace_zone ze7 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
["GET /__trace/session?path=/&max=10", "GET /__trace/sessions"]
--- response_body_like eval
[qr/"id":/, qr/"max_transactions":10/]
--- error_code eval
[200, 200]

=== TEST 8: D22 — ttl=0 falls back to retention (still created)
--- http_config
    trace_zone ze8 1m;
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/session?path=/&ttl=0
--- error_code: 200
--- response_body_like: "id"

=== TEST 9: D24 — empty sessions list carries "dropped":0
--- http_config
    trace_zone ze9 1m;
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/sessions
--- error_code: 200
--- response_body: {"sessions":[],"nsessions":0,"dropped":0}

=== TEST 10: M9.3 — session detail returns canonical id/state shape
--- http_config
    trace_zone ze10 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
["GET /__trace/session?path=/ex10", "GET /__trace/sessions"]
--- response_body_like eval
[qr/"id":/, qr/"filter":\{/]
--- error_code eval
[200, 200]

=== TEST 11: AC-14 — traced request produces steps in transaction JSON via emit API
--- http_config
    trace_zone ze11 1m;
--- config
    location /trace {
        trace on;
        return 200 "emit test";
    }
    location /__trace/ { trace_control; }
--- request eval
["GET /trace", "GET /__trace/last"]
--- response_body_like eval
["emit test", qr/"steps":\s*\[/]
--- error_code eval
[200, 200]

=== TEST 12: AC-14 — steps carry phase, handler, status, and t_offset_us
--- http_config
    trace_zone ze12 1m;
--- config
    location /trace {
        trace on;
        return 200 "step detail test";
    }
    location /__trace/ { trace_control; }
--- request eval
["GET /trace", "GET /__trace/last"]
--- response_body_like eval
["step detail test", qr/"phase":"\w+".*"handler":.*"status":"\w+".*"t_offset_us":\d+/s]
--- error_code eval
[200, 200]

=== TEST 13: AC-14 — GET /last returns 404 when ring is empty
--- http_config
    trace_zone ze13 1m;
--- config
    location /__trace/ { trace_control; }
--- request
GET /__trace/last
--- error_code: 404

=== TEST 14: M9.1 — GET /session with all query parameters (max, ttl, fault_only, fault_code)
--- http_config
    trace_zone ze14 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
["GET /__trace/session?path=/all&max=8&ttl=120&fault_only=1&fault_code=500",
 "GET /__trace/sessions"]
--- response_body_like eval
[qr/"id":/, qr/"max_transactions":8.*"fault_only":true.*"fault_code":500/s]
--- error_code eval
[200, 200]

=== TEST 15: M9.1 — GET /session returns 429 when max_sessions reached
--- http_config
    trace_zone ze15 1m;
    trace_max_sessions 1;
--- config
    location /__trace/ { trace_control; }
--- request eval
["GET /__trace/session?path=/a",
 "GET /__trace/session?path=/b"]
--- error_code eval
[200, 429]

=== TEST 16: M9.3 — transaction JSON via /last contains txn, worker_pid, connection_id
--- http_config
    trace_zone ze16 1m;
--- config
    location /trace {
        trace on;
        return 200 "full detail";
    }
    location /__trace/ { trace_control; }
--- request eval
["GET /trace", "GET /__trace/last"]
--- response_body_like eval
["full detail", qr/"txn":"trace".*"worker_pid":[1-9].*"connection_id":[1-9]/s]
--- error_code eval
[200, 200]

=== TEST 17: M10.3 — session JSON buffer handles 128-char ASCII path (1024B cap)
--- http_config
    trace_zone ze17 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
["POST /__trace/sessions?path=/aaaaaaaaaabbbbbbbbbbccccccccccddddddddddeeeeeeeeeeffffffffffgggggggggghhhhhhhhhhiiiiiiiiiijjjjjjjjjjkkkkkkkkkkllllllllllmmmmmmmmmmnn"]
--- response_body_like eval
[qr/"state":\s*"capturing"/]
--- error_code eval
[201]

=== TEST 18: M10.4 — fault_code validation: 500 OK, out-of-range coerced to 0
--- http_config
    trace_zone ze18 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
["POST /__trace/sessions?path=/ta&fault_code=5",
 "POST /__trace/sessions?path=/tb&fault_code=600",
 "POST /__trace/sessions?path=/tc&fault_code=500"]
--- response_body_like eval
[
    qr/"state":\s*"capturing"/,
    qr/"state":\s*"capturing"/,
    qr/"state":\s*"capturing"/
]
--- error_code eval
[201, 201, 201]

=== TEST 19: M10.4 — out-of-range fault_code=99 stored as 0, 500 stored correctly
--- http_config
    trace_zone ze19 1m;
--- config
    location /__trace/ { trace_control; }
--- request eval
[
    "POST /__trace/sessions?path=/tf19&fault_code=500",
    "POST /__trace/sessions?path=/tf19b&fault_code=99",
    "GET /__trace/sessions"
]
--- response_body_like eval
[
    qr/"state":\s*"capturing"/,
    qr/"state":\s*"capturing"/,
    qr/"fault_code":\s*500/
]
--- error_code eval
[201, 201, 200]

=== TEST 20: M10.5 — fault_code in transaction JSON via GET /last
--- http_config
    trace_zone ze20 1m;
--- config
    location /__trace/ { trace_control; }
    location /tf20 {
        trace on;
        return 500 "boom";
    }
--- request eval
[
    "POST /__trace/sessions?fault_only=1&path=/tf20&fault_code=500",
    "GET /tf20",
    "GET /__trace/last?since=0"
]
--- response_body_like eval
[
    qr/"state":\s*"capturing"/,
    "boom",
    qr/"fault":\s*\{.*"code":\s*500/s
]
--- error_code eval
[201, 500, 200]

=== TEST 21: M10.2 — transaction JSON has worker_pid/connection_id/status (valid shape)
--- http_config
    trace_zone ze21 1m;
--- config
    location /__trace/ { trace_control; }
    location /ring21 {
        trace on;
        return 200 "ok";
    }
--- request eval
["GET /ring21", "GET /__trace/last?since=0"]
--- response_body_like eval
["ok", qr/"txn":"trace".*"worker_pid":[1-9].*"connection_id":[1-9].*"status":200/s]
--- error_code eval
[200, 200]
