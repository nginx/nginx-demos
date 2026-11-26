# vi:set ft=perl:
#
# M0.4 — shared-memory ring-buffer round-trip (make-or-break primitive #2).
#
# Proves FR-SHM-1 + FR-API-6: a request worker commits a captured "hello
# timeline" transaction into a slab-backed shm zone, and a *control* endpoint
# reads it back out of that same zone. Because the control request may be served
# by a different worker than the one that captured the transaction, a successful
# read-back demonstrates the cross-worker shared-memory foundation that all later
# session storage is built on.
#
# The transaction is emitted as JSON so later milestones can grow the schema
# without changing the round-trip contract.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

run_tests();

__DATA__

=== TEST 1: proxied request is captured, control endpoint returns it as JSON
--- http_config
    # trace_zone declares the slab zone that holds captured transactions.
    trace_zone trace 1m;
--- config
    location = /front {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/back;
    }

    location = /back {
        return 200 "backend-body";
    }

    # Control endpoint: reads the last captured transaction from the shm zone.
    location = /trace/last {
        trace_control;
    }
--- request eval
["GET /front", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["backend-body", qr/"method"\s*:\s*"GET"/]

=== TEST 2: captured transaction records the request path and a status
--- http_config
    trace_zone trace 1m;
--- config
    location = /api {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/origin;
    }
    location = /origin {
        return 201 "created";
    }
    location = /trace/last {
        trace_control;
    }
--- request eval
["GET /api", "GET /trace/last"]
--- error_code eval
[201, 200]
--- response_body_like eval
["created", qr/"uri"\s*:\s*"\/api"/]

=== TEST 3: control endpoint is valid JSON with a steps array
--- http_config
    trace_zone trace 1m;
--- config
    location = /hit {
        trace on;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/z;
    }
    location = /z { return 200 "z"; }
    location = /trace/last { trace_control; }
--- request eval
["GET /hit", "GET /trace/last"]
--- error_code eval
[200, 200]
--- response_body_like eval
["z", qr/"steps"\s*:\s*\[/]
