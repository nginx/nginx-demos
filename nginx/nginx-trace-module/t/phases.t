# vi:set ft=perl ts=4 sw=4 et:
#
# M0.2 — Pass-through phase handlers must leave routing unchanged.
#
# The module now registers an observer handler in every *registrable* HTTP phase
# (POST_READ, SERVER_REWRITE, REWRITE, PREACCESS, ACCESS, PRECONTENT, LOG), each
# returning NGX_DECLINED (or NGX_OK for LOG) so nginx proceeds exactly as if the
# handler were absent (FR-PHASE-1/2; skill:handler-phase-registration).
#
# These tests exercise the phases whose behaviour a mis-registered / non-declining
# handler would break: rewrite, try_files (PRECONTENT), auth_request (ACCESS),
# error_page, and internal index/redirect. If any handler returned the wrong
# code, one of these would change status or routing.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

run_tests();

__DATA__

=== TEST 1: REWRITE phase unaffected — rewrite still rewrites
--- config
    location = /r {
        rewrite ^ /rewritten last;
    }
    location = /rewritten {
        return 200 "rewritten-ok";
    }
--- request
GET /r
--- response_body chomp
rewritten-ok
--- error_code: 200
--- no_error_log
[error]

=== TEST 2: PRECONTENT phase unaffected — try_files still resolves
--- config
    location = /tf {
        try_files /nonexistent @fallback;
    }
    location @fallback {
        return 200 "fallback-ok";
    }
--- request
GET /tf
--- response_body chomp
fallback-ok
--- error_code: 200
--- no_error_log
[error]

=== TEST 3: ACCESS phase unaffected — auth_request still gates (403)
# NOTE: the protected location must NOT produce its body via `return` (that
# fires in REWRITE, before the ACCESS phase where auth_request runs). Use a
# proxied content location so auth_request actually executes.
--- config
    location = /secure {
        auth_request /auth;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/backend;
    }
    location = /backend {
        return 200 "should-not-reach";
    }
    location = /auth {
        return 403;
    }
--- request
GET /secure
--- error_code: 403
--- no_error_log
[error]

=== TEST 4: ACCESS phase unaffected — auth_request allows (200)
--- config
    location = /open {
        auth_request /ok;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/backend;
    }
    location = /backend {
        return 200 "allowed";
    }
    location = /ok {
        return 204;
    }
--- request
GET /open
--- response_body chomp
allowed
--- error_code: 200
--- no_error_log
[error]

=== TEST 5: error_page interception still works (routing intact)
--- config
    location = /boom {
        return 500;
    }
    error_page 500 = /handled;
    location = /handled {
        return 200 "handled-ok";
    }
--- request
GET /boom
--- response_body chomp
handled-ok
--- error_code: 200
--- no_error_log
[error]

=== TEST 6: LOG phase handler does not disturb the response
--- config
    location = /logged {
        return 201 "created";
    }
--- request
GET /logged
--- response_body chomp
created
--- error_code: 201
--- no_error_log
[error]

=== TEST 7: SERVER_REWRITE phase unaffected — server-level rewrite still applies
# A rewrite in the server context runs in the SERVER_REWRITE phase (earlier than
# the location REWRITE of TEST 1). This proves the POST_READ + SERVER_REWRITE
# observer handlers also decline cleanly and do not alter early-phase routing.
--- config
    rewrite ^/srv-old$ /srv-new last;
    location = /srv-new {
        return 200 "server-rewritten-ok";
    }
--- request
GET /srv-old
--- response_body chomp
server-rewritten-ok
--- error_code: 200
--- no_error_log
[error]
