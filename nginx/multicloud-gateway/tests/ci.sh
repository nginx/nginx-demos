#!/usr/bin/env bash
# Smoke test: builds the test backend, starts it, and checks it answers rule lookups. Run from the
# repo root with .github/scripts/smoke.sh nginx/multicloud-gateway.
set -euo pipefail
# shellcheck source=../../../.github/scripts/smoke-lib.sh
source "$(git rev-parse --show-toplevel)/.github/scripts/smoke-lib.sh"

build multicloud-backend test-backend-db
start multicloud-backend 15010:5000 smoke-multicloud-backend

wait_http http://127.0.0.1:15010/mcgw/keys
expect "all rules" 200 '"fqdn":"api.ff.lan"' http://127.0.0.1:15010/mcgw/keys
expect "rule by query" 200 '"uri":"/getmyip"' 'http://127.0.0.1:15010/mcgw?fqdn=api.ff.lan&uri=/getmyip'
expect "unknown rule" 404 '"error":"Not found"' 'http://127.0.0.1:15010/mcgw?fqdn=nope&uri=/nope'
