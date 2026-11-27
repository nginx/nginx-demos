#!/usr/bin/env bash
# Smoke test: builds the module image, starts it with the demo's backend (as docker-compose.yml
# does, on a high port), and runs the module's own tests/smoke.sh against it. Run from the repo
# root with .github/scripts/smoke.sh nginx/ngx_http_greylist_module.
set -euo pipefail
# shellcheck source=../../../.github/scripts/smoke-lib.sh
source "$(git rev-parse --show-toplevel)/.github/scripts/smoke-lib.sh"

backend=$(sed -n 's/^ *image: \(hashicorp\/http-echo:.*\)$/\1/p' docker-compose.yml)

build greylist .

docker network create smoke-greylist >/dev/null
trap 'SMOKE_FAILED=${SMOKE_FAILED:-$?}; smoke_cleanup; docker network rm smoke-greylist >/dev/null 2>&1 || true' EXIT

echo "==> start greylist-backend"
docker run -d --name smoke-greylist-backend --network smoke-greylist --network-alias backend \
  "$backend" -listen=:8080 "-text=Upstream OK" >/dev/null
SMOKE_CONTAINERS+=(smoke-greylist-backend)
start greylist 15080:80 --network smoke-greylist smoke-greylist

wait_http http://127.0.0.1:15080/healthz
tests/smoke.sh http://127.0.0.1:15080
