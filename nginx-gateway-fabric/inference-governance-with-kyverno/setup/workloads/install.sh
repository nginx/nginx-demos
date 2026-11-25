#!/usr/bin/env bash
# Installs the demo's starting state: namespaces, identities, shared models, team-research's
# model, the gateway, the allowlist and the baseline routes. No policies are installed here.
set -euo pipefail
cd "$(dirname "$0")/../.."
# shellcheck source=../../lab/versions.env
source lab/versions.env

step() { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }

LLMD_ROUTER_DIR=${LLMD_ROUTER_DIR:-.cache/llm-d-router-${LLMD_ROUTER_VERSION}}
export LLMD_ROUTER_DIR LLMD_ROUTER_VERSION INFERENCE_SIM_VERSION
if [ ! -d "$LLMD_ROUTER_DIR" ]; then
  step "Fetching llm-d-router $LLMD_ROUTER_VERSION (for the EPP/InferencePool chart)"
  mkdir -p "$LLMD_ROUTER_DIR"
  curl -sSL "https://codeload.github.com/llm-d/llm-d-router/tar.gz/refs/tags/$LLMD_ROUTER_VERSION" \
    | tar xz -C "$LLMD_ROUTER_DIR" --strip-components=1
fi

step "Namespaces, identities, RBAC (tenants, and what Kyverno may read and generate)"
kubectl apply -f setup/workloads/manifests/00-namespaces.yaml -f setup/workloads/manifests/01-rbac.yaml -f setup/workloads/manifests/02-kyverno-permissions.yaml

step "Simulator datasets"
mkdir -p .cache/datasets
for ds in general research-internal vendor; do
  python3 setup/workloads/datasets/build.py "setup/workloads/datasets/$ds.txt" ".cache/datasets/$ds.sqlite3"
done
for ns in models team-research team-assistant vendor; do
  for ds in general research-internal vendor; do
    kubectl -n "$ns" create configmap "dataset-$ds" \
      --from-file=dataset.sqlite3=".cache/datasets/$ds.sqlite3" --dry-run=client -o yaml | kubectl apply -f -
  done
done

step "Models: shared llama-8b (l4), llama-70b (h100); team-research's research-ft (h100)"
#                 name        namespace      gpu   dataset            cost-center    failureMode
setup/workloads/models.sh     llama-8b    models         l4    general            platform       FailClose | kubectl apply -f -
setup/workloads/models.sh     llama-70b   models         h100  general            platform       FailClose | kubectl apply -f -
setup/workloads/models.sh     research-ft team-research  h100  research-internal  team-research  FailClose | kubectl apply -f -

step "Mock external model vendor (for the external-fallback example)"
kubectl apply -f setup/workloads/manifests/15-vendor.yaml

step "Gateway, shared-model ReferenceGrant, allowlist, baseline routes"
kubectl apply -f setup/workloads/manifests/10-gateway.yaml -f setup/workloads/manifests/20-allowlist.yaml -f setup/workloads/manifests/30-routes.yaml

step "Waiting for workloads"
for ns in models team-research vendor; do
  kubectl -n "$ns" wait deploy --all --for=condition=Available --timeout=300s
done
kubectl -n gateway wait gateway/inference-gateway --for=condition=Programmed --timeout=300s
# Programmed means NGF accepted the config, not that the NGINX pod it provisions is serving yet.
echo "waiting for the NGINX data plane in namespace gateway"
for _ in $(seq 1 60); do
  kubectl -n gateway get deploy -o name 2>/dev/null | grep -q . && break
  sleep 2
done
kubectl -n gateway wait deploy --all --for=condition=Available --timeout=300s

step "Workloads installed"
