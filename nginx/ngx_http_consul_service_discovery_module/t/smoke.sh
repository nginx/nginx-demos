#!/usr/bin/env bash
# t/smoke.sh — integration smoke test
#
# Verified:
#   1. nginx -t config validation
#   2. Discovery endpoint responds
#   3. Both service names present
#   4. Ports auto-discovered from listen (19877, 19878)
#   5. Address auto-discovered from listen IP ("0.0.0.0" for wildcard)
#   6. Content-Type: application/json
#   7. {"services":[...]} top-level shape

set -euo pipefail

NGINX_BIN="${1:?Usage: smoke.sh <nginx-binary> <nginx-conf>}"
NGINX_CONF="${2:?Usage: smoke.sh <nginx-binary> <nginx-conf>}"

DISCOVERY_URL="http://127.0.0.1:19876/consul/services"
PID_FILE="/tmp/ngx-consul-test.pid"
LOG_FILE="/tmp/ngx-consul-test-error.log"
MAX_WAIT=10

stop_nginx() {
    if [ -f "$PID_FILE" ]; then
        kill "$(cat "$PID_FILE")" 2>/dev/null || true
        rm -f "$PID_FILE"
    fi
}
trap stop_nginx EXIT

echo "→ Validating config..."
"$NGINX_BIN" -t -c "$NGINX_CONF" -p /tmp/

echo "→ Starting NGINX..."
"$NGINX_BIN" -c "$NGINX_CONF" -p /tmp/

echo "→ Waiting for $DISCOVERY_URL..."
elapsed=0
until curl -sf "$DISCOVERY_URL" > /dev/null 2>&1; do
    sleep 1
    elapsed=$((elapsed + 1))
    if [ "$elapsed" -ge "$MAX_WAIT" ]; then
        echo "✗ Timed out after ${MAX_WAIT}s"
        cat "$LOG_FILE" 2>/dev/null || true
        exit 1
    fi
done

RESPONSE=$(curl -sf "$DISCOVERY_URL")
HEADERS=$(curl -sI "$DISCOVERY_URL")
echo "→ Response: $RESPONSE"

# Service names
for svc in "api-a" "api-b"; do
    if echo "$RESPONSE" | grep -q "\"$svc\""; then
        echo "✓ Service name '$svc' found"
    else
        echo "✗ Service name '$svc' NOT found"; exit 1
    fi
done

# Ports from listen directive
for port in "19877" "19878"; do
    if echo "$RESPONSE" | grep -q "\"port\":${port}"; then
        echo "✓ Port $port auto-discovered"
    else
        echo "✗ Port $port NOT found"; echo "  $RESPONSE"; exit 1
    fi
done

# Address from listen directive (wildcard → "0.0.0.0")
if echo "$RESPONSE" | grep -q '"address":"0.0.0.0"'; then
    echo "✓ Address \"0.0.0.0\" auto-discovered from listen"
else
    echo "✗ Address \"0.0.0.0\" NOT found"; echo "  $RESPONSE"; exit 1
fi

# Content-Type
if echo "$HEADERS" | grep -qi "content-type: application/json"; then
    echo "✓ Content-Type: application/json"
else
    echo "✗ Wrong Content-Type"; echo "$HEADERS"; exit 1
fi

# JSON shape
if echo "$RESPONSE" | grep -qE '^\{"services":\['; then
    echo "✓ Top-level JSON shape correct"
else
    echo "✗ Unexpected JSON shape: $RESPONSE"; exit 1
fi

echo ""
echo "✓ All smoke tests passed"
