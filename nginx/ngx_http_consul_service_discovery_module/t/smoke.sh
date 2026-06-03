#!/usr/bin/env bash
# t/smoke.sh — smoke tests for ngx_http_consul_service_discovery (v2)
#
# Usage: bash t/smoke.sh /path/to/nginx-<version>  [nginx-binary]
#
# Verifies:
#   1.  nginx -t  config validation passes
#   2.  NGINX starts and the discovery endpoint responds
#   3.  Both test services appear in JSON by name
#   4.  Ports are correctly auto-discovered from listen
#   5.  Addresses are correctly auto-discovered from listen
#   6.  Content-Type: application/json header is present
#   7.  Response has {"services":[...]} top-level shape
#   8.  tags is a JSON array (not a plain string)
#   9.  id field is present on every service
#  10.  meta object is present when configured
#  11.  checks array is present when configured

set -euo pipefail

NGINX_SRC="${1:-/usr/src/nginx}"
NGINX_BIN="${2:-$(command -v nginx 2>/dev/null || echo "${NGINX_SRC}/objs/nginx")}"
MODULE_SO="${NGINX_SRC}/objs/ngx_http_consul_service_discovery_module.so"
TMPDIR=$(mktemp -d)
trap 'kill $NGINX_PID 2>/dev/null; rm -rf "$TMPDIR"' EXIT

# ── Write a test nginx.conf ────────────────────────────────────────────────
CONF="${TMPDIR}/nginx.conf"
PIDFILE="${TMPDIR}/nginx.pid"

cat > "$CONF" <<EOF
load_module ${MODULE_SO};
daemon     on;
pid        ${PIDFILE};
error_log  ${TMPDIR}/error.log warn;

events { worker_connections 64; }

http {
    access_log off;

    server {
        listen 18880;
        server_name svc-a;
        consul_service_discoverable on;
        consul_service_name         "svc-a";
        consul_service_id           "svc-a-v1";
        consul_service_tags         "v1,http";
        consul_service_meta         env  prod;
        consul_service_check name=tcp tcp=127.0.0.1:18880 interval=10s timeout=1s;
        location / { return 200; }
    }

    server {
        listen 18881;
        server_name svc-b;
        consul_service_discoverable on;
        consul_service_name         "svc-b";
        # id intentionally omitted → should auto-generate "svc-b-v2"
        consul_service_tags         "v2,grpc";
        consul_service_meta         env  staging;
        consul_service_meta         team backend;
        location / { return 200; }
    }

    server {
        listen 9090;
        server_name discovery.local;
        location /consul/services {
            consul_service_discovery on;
        }
    }
}
EOF

PASS=0; FAIL=0
ok()   { echo "  PASS: $*"; ((PASS++)); }
fail() { echo "  FAIL: $*"; ((FAIL++)); }

echo "=== ngx_http_consul_service_discovery smoke tests ==="
echo ""

# 1. Config validation
echo "[1] nginx -t"
if "$NGINX_BIN" -t -c "$CONF" 2>/dev/null; then ok "nginx -t passes"
else fail "nginx -t failed"; cat "${TMPDIR}/error.log"; exit 1; fi

# Start NGINX
"$NGINX_BIN" -c "$CONF"
sleep 0.5
NGINX_PID=$(cat "$PIDFILE" 2>/dev/null || echo "")

# 2. Endpoint responds
echo "[2] Endpoint responds"
RESP=$(curl -sf http://127.0.0.1:9090/consul/services) && ok "HTTP 200 OK" || fail "no response"

# 3. Services appear by name
echo "[3] Both services appear"
echo "$RESP" | jq -e '.services[] | select(.name=="svc-a")' > /dev/null \
    && ok "svc-a found" || fail "svc-a missing"
echo "$RESP" | jq -e '.services[] | select(.name=="svc-b")' > /dev/null \
    && ok "svc-b found" || fail "svc-b missing"

# 4. Ports auto-discovered
echo "[4] Ports auto-discovered"
echo "$RESP" | jq -e '.services[] | select(.name=="svc-a" and .port==18880)' > /dev/null \
    && ok "svc-a port=18880" || fail "svc-a port wrong"
echo "$RESP" | jq -e '.services[] | select(.name=="svc-b" and .port==18881)' > /dev/null \
    && ok "svc-b port=18881" || fail "svc-b port wrong"

# 5. Address auto-discovered
echo "[5] Addresses auto-discovered"
echo "$RESP" | jq -e '.services[] | select(.name=="svc-a" and (.address|length > 0))' > /dev/null \
    && ok "svc-a address present" || fail "svc-a address missing"

# 6. Content-Type header
echo "[6] Content-Type: application/json"
CT=$(curl -sI http://127.0.0.1:9090/consul/services | grep -i content-type | tr -d '\r')
echo "$CT" | grep -qi "application/json" \
    && ok "Content-Type: application/json" || fail "wrong Content-Type: $CT"

# 7. Top-level shape
echo "[7] Top-level {\"services\":[...]} shape"
echo "$RESP" | jq -e 'has("services") and (.services|type=="array")' > /dev/null \
    && ok "shape OK" || fail "unexpected shape"

# 8. tags is a JSON array
echo "[8] tags is a JSON array"
echo "$RESP" | jq -e '.services[] | select(.name=="svc-a") | (.tags|type=="array")' > /dev/null \
    && ok "svc-a tags is array" || fail "svc-a tags is not array"
echo "$RESP" | jq -e '.services[] | select(.name=="svc-a") | .tags | contains(["v1","http"])' > /dev/null \
    && ok "svc-a tags values correct" || fail "svc-a tags values wrong"

# 9. id field present
echo "[9] id field present"
echo "$RESP" | jq -e '.services[] | select(.name=="svc-a") | .id == "svc-a-v1"' > /dev/null \
    && ok "svc-a explicit id" || fail "svc-a id wrong"
echo "$RESP" | jq -e '.services[] | select(.name=="svc-b") | .id == "svc-b-v2"' > /dev/null \
    && ok "svc-b auto id (svc-b-v2)" || fail "svc-b auto id wrong"

# 10. meta object
echo "[10] meta object"
echo "$RESP" | jq -e '.services[] | select(.name=="svc-a") | .meta.env == "prod"' > /dev/null \
    && ok "svc-a meta.env=prod" || fail "svc-a meta wrong"
echo "$RESP" | jq -e '.services[] | select(.name=="svc-b") | .meta.team == "backend"' > /dev/null \
    && ok "svc-b meta.team=backend" || fail "svc-b meta wrong"

# 11. checks array
echo "[11] checks array"
echo "$RESP" | jq -e '.services[] | select(.name=="svc-a") | .checks | length >= 1' > /dev/null \
    && ok "svc-a has checks" || fail "svc-a checks missing"
echo "$RESP" | jq -e '.services[] | select(.name=="svc-a") | .checks[0].tcp | length > 0' > /dev/null \
    && ok "svc-a check has tcp key" || fail "svc-a check.tcp missing"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ] || exit 1
