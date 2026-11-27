#!/usr/bin/env bash
# Smoke test: builds and starts the whole stack with docker compose (on high ports, under its own
# project name), then checks the MCP server directly and through the NGINX gateway: tool discovery,
# a tool call, RBAC, and that the UI, Prometheus and Grafana answer. Run from the repo root with
# .github/scripts/smoke.sh nginx/mcp-gateway-mission-control.
set -euo pipefail
# shellcheck source=../../../.github/scripts/smoke-lib.sh
source "$(git rev-parse --show-toplevel)/.github/scripts/smoke-lib.sh"

export COMPOSE_PROJECT_NAME=smoke-mcp NGINX_EDITOR_TOKEN=smoke-test-only \
  GATEWAY_PORT=15180 UI_PORT=15130 MCP_DIRECT_PORT=15101 GRAFANA_PORT=15132 PROMETHEUS_PORT=15190
# shellcheck disable=SC2154  # rc is set inside the trap
trap 'rc=$?; [ "$rc" = 0 ] || docker compose logs --tail 40; docker compose down -v --rmi local >/dev/null 2>&1 || true' EXIT

echo "==> docker compose up"
docker compose up -d --build --quiet-pull

mcp=(-X POST -H 'Content-Type: application/json' -H 'Accept: application/json, text/event-stream')
list='{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}'
call() { printf '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"%s","arguments":%s}}' "$1" "$2"; }

wait_http http://127.0.0.1:15101/health
expect "MCP server: tools/list" 200 '"name":"create_pod"' http://127.0.0.1:15101/mcp "${mcp[@]}" -d "$list"
expect "MCP server: defaults applied" 200 '\"namespace\": \"staging\"' http://127.0.0.1:15101/mcp "${mcp[@]}" \
  -d "$(call create_pod '{"name":"smoke"}')"
expect "MCP server: input validated" 200 'Input validation error' http://127.0.0.1:15101/mcp "${mcp[@]}" \
  -d "$(call get_pod '{"name":42}')"

wait_http http://127.0.0.1:15180/mcp
expect "gateway: viewer lists tools" 200 '"name":"list_pods"' http://127.0.0.1:15180/mcp "${mcp[@]}" \
  -H 'X-API-Key: viewer-demo' -d "$list"
expect "gateway: viewer calls list_pods" 200 'api-7d9c' http://127.0.0.1:15180/mcp "${mcp[@]}" \
  -H 'X-API-Key: viewer-demo' -d "$(call list_pods '{}')"
expect "gateway: viewer can't delete" 403 - http://127.0.0.1:15180/mcp "${mcp[@]}" \
  -H 'X-API-Key: viewer-demo' -d "$(call delete_pod '{"name":"api-7d9c"}')"

wait_http http://127.0.0.1:15130/
expect "UI" 200 - http://127.0.0.1:15130/
wait_http http://127.0.0.1:15190/-/ready
expect "Prometheus" 200 - http://127.0.0.1:15190/-/ready
for _ in $(seq 1 60); do
  [ "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:15132/api/health)" = 200 ] && break
  sleep 2
done
expect "Grafana" 200 '"database"' http://127.0.0.1:15132/api/health
