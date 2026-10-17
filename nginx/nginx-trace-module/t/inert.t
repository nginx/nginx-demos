# vi:set ft=perl ts=4 sw=4 et:
#
# M0.1 — Buildable empty dynamic module.
#
# Encodes the milestone "done-when" (IMPLEMENTATION_PLAN.md M0.1) test-first:
#   1. nginx -t passes with the module loaded via load_module (FR-CFG-18).
#   2. The module is inert: a request routes identically with the module
#      present, proving NGX_CONF_UNSET defaults leave behaviour unchanged
#      (NFR-PORT-1, the "near-zero cost when off" baseline / AC-1 precursor).
#
# The module .so is loaded by the top-level directive injected via
# TEST_NGINX_GLOBALS (see t/lib/setup or the Makefile), so this file only
# exercises HTTP behaviour.

use lib 'lib';
use Test::Nginx::Socket 'no_plan';

run_tests();

__DATA__

=== TEST 1: module loads and config parses (nginx -t equivalent)
--- config
    location = /t1 {
        return 200 "ok";
    }
--- request
GET /t1
--- response_body chomp
ok
--- error_code: 200
--- no_error_log
[error]
[alert]
[emerg]

=== TEST 2: inert — plain static location is unchanged with module loaded
--- config
    location = /t2 {
        add_header X-Probe probe;
        return 204;
    }
--- request
GET /t2
--- response_headers
X-Probe: probe
--- error_code: 204
--- no_error_log
[error]

=== TEST 3: inert — proxy route is untouched (routing identical)
--- config
    location = /t3 {
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/upstream;
    }
    location = /upstream {
        return 200 "from-upstream";
    }
--- request
GET /t3
--- response_body chomp
from-upstream
--- error_code: 200
--- no_error_log
[error]

=== TEST 4: inert — explicit `trace off;` parses and leaves routing unchanged
# Proves the trace directive is accepted in the location context and that
# disabling it changes nothing (the off state is the inert baseline).
--- config
    location = /t4 {
        trace off;
        add_header X-Trace-State off;
        return 200 "disabled-ok";
    }
--- request
GET /t4
--- response_body chomp
disabled-ok
--- response_headers
X-Trace-State: off
--- error_code: 200
--- no_error_log
[error]
[alert]
[emerg]
