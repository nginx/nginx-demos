#!/usr/bin/env bash
# register-services.sh
# Poll the NGINX service-discovery endpoint and register/deregister with Consul.
#
# Usage:
#   DISCOVERY_URL=http://127.0.0.1:8080/consul/services \
#   CONSUL_URL=http://127.0.0.1:8500                     \
#   ./register-services.sh
#
# The JSON served by NGINX now carries:
#   id, name, address, port, tags (array), meta (object), checks (array).
# All fields are passed through to the Consul /v1/agent/service/register API
# verbatim, so no manual field mapping is required beyond what jq does below.

set -euo pipefail

DISCOVERY_URL="${DISCOVERY_URL:-http://127.0.0.1:8080/consul/services}"
CONSUL_URL="${CONSUL_URL:-http://127.0.0.1:8500}"
REGISTER_URL="${CONSUL_URL}/v1/agent/service/register"
DEREGISTER_URL="${CONSUL_URL}/v1/agent/service/deregister"

# ── Fetch catalogue ────────────────────────────────────────────────────────
catalogue=$(curl -sf "$DISCOVERY_URL") || {
    echo "[ERROR] Cannot reach $DISCOVERY_URL" >&2
    exit 1
}

# ── Deregister services no longer in catalogue (optional) ─────────────────
registered=$(curl -sf "${CONSUL_URL}/v1/agent/services" | jq -r 'keys[]') || true

catalogue_ids=$(echo "$catalogue" | jq -r '.services[].id')

for reg_id in $registered; do
    if ! echo "$catalogue_ids" | grep -qx "$reg_id"; then
        echo "Deregistering stale service: $reg_id"
        curl -sf -X PUT "${DEREGISTER_URL}/${reg_id}" || true
    fi
done

# ── Register / re-register every service in catalogue ─────────────────────
echo "$catalogue" | jq -c '.services[]' | while IFS= read -r svc; do
    id=$(echo "$svc"      | jq -r '.id')
    name=$(echo "$svc"    | jq -r '.name')
    address=$(echo "$svc" | jq -r '.address')
    port=$(echo "$svc"    | jq -r '.port')
    tags=$(echo "$svc"    | jq -c '.tags')          # already an array
    meta=$(echo "$svc"    | jq -c '.meta // {}')
    checks=$(echo "$svc"  | jq -c '.checks // []')

    # Build the Consul registration payload.
    # Tags is already a JSON array; meta is already an object.
    # Checks: map our {name,tcp/http/grpc,interval,timeout} shape to Consul's.
    consul_checks=$(echo "$checks" | jq -c '
        [ .[] | {
            Name:     .name,
            Interval: .interval,
            Timeout:  .timeout,
            TCP:      (if .tcp  then .tcp  else null end),
            HTTP:     (if .http then .http else null end),
            GRPC:     (if .grpc then .grpc else null end),
            TTL:      (if .ttl  then .ttl  else null end)
        } | del(.[] | nulls) ]
    ')

    payload=$(jq -n \
        --arg     ID      "$id"      \
        --arg     Name    "$name"    \
        --arg     Address "$address" \
        --argjson Port    "$port"    \
        --argjson Tags    "$tags"    \
        --argjson Meta    "$meta"    \
        --argjson Checks  "$consul_checks" \
        '{ID:$ID, Name:$Name, Address:$Address, Port:$Port,
          Tags:$Tags, Meta:$Meta, Checks:$Checks}')

    if curl -sf -X PUT "$REGISTER_URL" \
            -H "Content-Type: application/json" \
            -d "$payload" > /dev/null; then
        echo "Registered: $id ($name) at $address:$port  tags=$tags"
    else
        echo "[WARN] Failed to register $id" >&2
    fi
done
