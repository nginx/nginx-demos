#!/usr/bin/env bash
# Smoke test: the image build compiles and runs the parser unit tests, then builds NGINX with the
# module. This starts it and sends DNS queries over UDP and TCP with the module's
# test/test_nginx.py. Run from the repo root with
# .github/scripts/smoke.sh nginx/ngx_stream_dns_module.
set -euo pipefail
# shellcheck source=../../../.github/scripts/smoke-lib.sh
source "$(git rev-parse --show-toplevel)/.github/scripts/smoke-lib.sh"

build dns .
start dns 15353:5353 smoke-dns

docker exec smoke-dns nginx -t
for i in $(seq 1 30); do
  docker exec smoke-dns python3 /usr/src/ngx_stream_dns_module/test/test_nginx.py 127.0.0.1 5353 && break
  [ "$i" -lt 30 ] || { SMOKE_FAILED=1; exit 1; }
  sleep 1
done
echo "ok: UDP and TCP queries accepted"
