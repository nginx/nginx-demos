#!/usr/bin/env bash
# Smoke test: builds the NGINX Open Source image with NGINX Agent (the NGINX Plus images need a
# license, so they aren't built here), checks NGINX serves, and runs a yq edit on the Agent's config
# like container/start.sh does. Run from the repo root with
# .github/scripts/smoke.sh nginx/docker-image-builder.
set -euo pipefail
# shellcheck source=../../../.github/scripts/smoke-lib.sh
source "$(git rev-parse --show-toplevel)/.github/scripts/smoke-lib.sh"

build image-builder-oss . -f Dockerfile.oss --build-arg NGINX_AGENT=true
start image-builder-oss 15280:80 smoke-image-builder-oss

wait_http http://127.0.0.1:15280/
expect "NGINX serves" 200 'Welcome to nginx' http://127.0.0.1:15280/

echo "==> yq edits the Agent config, as start.sh does"
docker exec -e NGINX_AGENT_LOG_LEVEL=debug smoke-image-builder-oss sh -c '
  yq --version
  yq -i ".log.level=strenv(NGINX_AGENT_LOG_LEVEL)" /etc/nginx-agent/nginx-agent.conf
  test "$(yq .log.level /etc/nginx-agent/nginx-agent.conf)" = debug'
echo "ok: yq works in the image"
