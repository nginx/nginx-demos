#!/usr/bin/env bash
# Collects what's needed to debug "traffic doesn't reach the gateway". Read-only.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
source versions.env

h() { printf '\n\033[1;34m== %s\033[0m\n' "$*"; }

h "Data plane pods (gateway namespace)"
kubectl -n gateway get pods -o wide
h "Data plane Service (expect type NodePort, port 80 -> nodePort 30080)"
kubectl -n gateway get svc -o wide
kubectl -n gateway get svc -o jsonpath='{range .items[*]}{.metadata.name}{": "}{range .spec.ports[*]}{.name}={.port}->{.nodePort} {end}{"\n"}{end}'
h "Endpoints behind it"
kubectl -n gateway get endpointslices -o wide
h "Gateway status"
kubectl -n gateway get gateway inference-gateway -o jsonpath='{range .status.conditions[*]}{.type}={.status} {.reason}: {.message}{"\n"}{end}'
h "Effective NginxProxy (GatewayClass level, from Helm)"
kubectl get nginxproxies -A -o yaml | yq '.items[] | {"name": .metadata.namespace + "/" + .metadata.name, "spec": .spec}'
h "Route status"
kubectl get httproutes -A -o jsonpath='{range .items[*]}{.metadata.namespace}/{.metadata.name}: {range .status.parents[*].conditions[*]}{.type}={.status}({.reason}) {end}{"\n"}{end}'
h "Data plane pod events and recent logs"
pod=$(kubectl -n gateway get pods -o name | head -1)
if [ -n "$pod" ]; then
  kubectl -n gateway describe "$pod" | sed -n '/^Events:/,$p' | tail -15
  kubectl -n gateway logs "$pod" --all-containers --tail=30 2>&1 | tail -30
fi
h "NGF control plane errors (last 20)"
kubectl -n nginx-gateway logs deploy/ngf-nginx-gateway-fabric --tail=200 2>&1 | grep -iE '"level":"error"|error' | tail -20
h "From the kind node itself"
docker exec "$CLUSTER_NAME-control-plane" curl -s -m 5 -o /dev/null -w 'node:30080 -> HTTP %{http_code}\n' \
  -H 'Host: assistant.example' http://localhost:30080/ || echo "node:30080 -> no answer"
h "From the host"
curl -s -m 5 -o /dev/null -w "localhost:8080 -> HTTP %{http_code}\n" -H 'Host: assistant.example' "$GATEWAY_URL/" \
  || echo "localhost:8080 -> no answer"
docker port "$CLUSTER_NAME-control-plane"

h "Vendor fallback, repo only (ExternalName via the NginxProxy DNS resolver)"
kubectl -n team-assistant get svc vendor-fallback -o wide 2>/dev/null || echo "vendor-fallback not created yet"
kubectl -n team-assistant get httproute assistant-fallback \
  -o jsonpath='{range .status.parents[*].conditions[*]}{.type}={.status} {.reason}: {.message}{"\n"}{end}' 2>/dev/null
kubectl -n vendor get pods,svc 2>/dev/null
curl -s -m 5 -w '\nHTTP %{http_code}\n' "$GATEWAY_URL/v1/chat/completions" -H 'Host: assistant-fallback.example' \
  -H 'Content-Type: application/json' -d '{"model":"auto","max_tokens":10,"messages":[{"role":"user","content":"hi"}]}' 2>&1 | tail -3
