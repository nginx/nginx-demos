#!/bin/sh
# Pre-demo reset: return the whole stack to a known-clean state.
#  - restores pods/records (MCP server state is in memory)
#  - discards live NGINX edits (nginx container reloads the repo nginx.conf)
#  - clears the editor control volume, rate-limit counters and Prometheus history
# Does NOT rebuild images, so it works offline.
set -eu
cd "$(dirname "$0")/.."

# Pick up port overrides from .env (same names as compose.yaml).
port() { v=$(sed -n "s/^$1=//p" .env 2>/dev/null | tail -1); echo "${v:-$2}"; }
GATEWAY_PORT=$(port GATEWAY_PORT 8080); MCP_DIRECT_PORT=$(port MCP_DIRECT_PORT 8001)
UI_PORT=$(port UI_PORT 3000); GRAFANA_PORT=$(port GRAFANA_PORT 3002)

if [ -n "$(git status --porcelain -- nginx/nginx.conf 2>/dev/null)" ]; then
  echo "WARNING: nginx/nginx.conf has uncommitted changes; the demo will start from that file."
fi

docker compose down --volumes --remove-orphans
docker compose up -d

printf 'Waiting for gateway'
i=0
until curl -fsS -o /dev/null http://127.0.0.1:$GATEWAY_PORT/health 2>/dev/null; do
  i=$((i + 1)); [ "$i" -ge 60 ] && { echo; echo "Gateway not healthy after 60s; check: docker compose logs nginx"; exit 1; }
  printf '.'; sleep 1
done
echo ' ready.'
curl -fsS http://127.0.0.1:$MCP_DIRECT_PORT/health && echo
echo "Reset complete. Mission Control: http://localhost:$UI_PORT  Grafana: http://localhost:$GRAFANA_PORT"
