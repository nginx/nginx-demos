#!/usr/bin/env bash
# Smoke test: builds the module against the NGINX in the pinned base image, runs the module's own
# t/smoke.sh in the build stage (it starts NGINX on localhost and checks the discovery endpoint),
# then checks the final image's config loads the module. Run from the repo root with
# .github/scripts/smoke.sh nginx/ngx_http_consul_service_discovery_module.
set -euo pipefail
# shellcheck source=../../../.github/scripts/smoke-lib.sh
source "$(git rev-parse --show-toplevel)/.github/scripts/smoke-lib.sh"

build consul-build . --target build
build consul .

echo "==> t/smoke.sh"
docker run --rm -v "$PWD/t:/t:ro" --entrypoint sh smoke-consul-build -c '
  apk add --no-cache bash curl jq >/dev/null
  src=$(ls -d /tmp/nginx-*/)
  bash /t/smoke.sh "${src%/}" "$(command -v nginx)"'

# examples/nginx.conf is a reference (it names hosts and certificates that only exist in a real
# deployment), so check the final image loads the module with a minimal config instead.
echo "==> final image loads the module"
docker run --rm --entrypoint sh smoke-consul -c '
  printf "load_module modules/ngx_http_consul_service_discovery_module.so;\nevents {}\nhttp { server { listen 8080; location /consul/services { consul_service_discovery on; } } }\n" > /tmp/nginx.conf
  nginx -t -c /tmp/nginx.conf'
echo "ok: module loads in the final image"
