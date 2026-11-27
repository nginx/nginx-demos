#!/usr/bin/env bash
# Smoke test: builds the backend and API server images (the NGINX Plus image needs a license, so
# it isn't built here), starts them, and checks each answers. Run from the repo root with
# .github/scripts/smoke.sh nginx/api-steering.
set -euo pipefail
# shellcheck source=../../../.github/scripts/smoke-lib.sh
source "$(git rev-parse --show-toplevel)/.github/scripts/smoke-lib.sh"

build api-steering-backend backend
build api-steering-apiserver apiserver

start api-steering-backend 15000:5000 -v "$PWD/backend/db-dockercompose.json:/app/db.json:ro" smoke-api-steering-backend
start api-steering-apiserver 15001:5000 smoke-api-steering-apiserver

wait_http http://127.0.0.1:15000/jwks.json
expect "backend: rule lookup" 200 '"uri":"v1.0/api_get"' http://127.0.0.1:15000/backend/fetchkey/v1.0/api_get
expect "backend: unknown rule" 404 '"error":"Not found"' http://127.0.0.1:15000/backend/fetchkey/nope
expect "backend: JWKS" 200 '"kid":"0001"' http://127.0.0.1:15000/jwks.json

# The API server serves HTTPS with an ad hoc certificate (needs cryptography).
wait_http https://127.0.0.1:15001/get_data -k
expect "apiserver: GET" 200 '"hostname"' https://127.0.0.1:15001/get_data -k
expect "apiserver: POST echo" 200 '"var":123' https://127.0.0.1:15001/echo_data -k \
  -X POST -H 'Content-Type: application/json' -d '{"var":123}'
