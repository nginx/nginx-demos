#!/usr/bin/env bash
# Is the environment ready to record? Exits non-zero on the first failed check.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
source versions.env

ok()   { printf '  \033[32mok\033[0m   %s\n' "$*"; }
bad()  { printf '  \033[31mFAIL\033[0m %s\n' "$*"; echo "  run tools/diagnose.sh for details"; exit 1; }
check() { local name=$1; shift; if "$@" >/dev/null 2>&1; then ok "$name"; else bad "$name"; fi; }

echo "Platform"
for crd in gateways.gateway.networking.k8s.io httproutes.gateway.networking.k8s.io \
           referencegrants.gateway.networking.k8s.io inferencepools.inference.networking.k8s.io \
           inferenceobjectives.llm-d.ai ratelimitpolicies.gateway.nginx.org \
           validatingpolicies.policies.kyverno.io generatingpolicies.policies.kyverno.io \
           deletingpolicies.policies.kyverno.io; do
  check "CRD $crd" kubectl get crd "$crd"
done
for ns in nginx-gateway kyverno policy-reporter; do
  check "deployments available in $ns" kubectl -n "$ns" wait deploy --all --for=condition=Available --timeout=5s
done

echo "Workloads"
check "gateway programmed" kubectl -n gateway wait gateway/inference-gateway --for=condition=Programmed --timeout=5s
for p in models/llama-8b models/llama-70b team-research/research-ft; do
  check "pool $p" kubectl -n "${p%/*}" get inferencepool "${p#*/}"
done
for ns in models team-research vendor; do
  check "deployments available in $ns" kubectl -n "$ns" wait deploy --all --for=condition=Available --timeout=5s
done

echo "Traffic"
check "NGINX data plane available" kubectl -n gateway wait deploy --all --for=condition=Available --timeout=5s
check "data plane Service is NodePort 30080" bash -c \
  "kubectl -n gateway get svc -o jsonpath='{.items[*].spec.ports[*].nodePort}' | grep -qw 30080"
answer() { tools/chat.sh "$1" 2>&1; }
# The data plane and endpoint pickers can take a few seconds after Available; retry briefly.
for _ in $(seq 1 15); do
  a=$(answer assistant.example)
  grep -q "model: llama-8b" <<<"$a" && break
  sleep 2
done
if grep -q "model: llama-8b" <<<"$a"; then ok "assistant.example answered by llama-8b"; else bad "assistant.example: $a"; fi
r=$(answer research.example)
if grep -q "INTERNAL:" <<<"$r"; then ok "research.example answered by research-ft"; else bad "research.example: $r"; fi

echo "Starting state"
n=$(kubectl get validatingpolicies,generatingpolicies,deletingpolicies -o name 2>/dev/null | wc -l | tr -d ' ')
if [ "$n" = 0 ]; then ok "no demo policies installed yet"; else bad "$n demo policies installed (run make reset)"; fi
check "cost meter sees the verify requests" bash -c 'tools/cost-meter.sh --once | grep -q llama-8b'

echo "Ready."
