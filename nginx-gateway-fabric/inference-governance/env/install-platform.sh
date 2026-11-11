#!/usr/bin/env bash
# Creates the kind cluster and installs the platform: Gateway API + Inference Extension CRDs,
# NGINX Gateway Fabric, Kyverno, Policy Reporter.
set -euo pipefail
cd "$(dirname "$0")/.."
# shellcheck source=../versions.env
source versions.env

step() { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }

if ! kind get clusters 2>/dev/null | grep -qx "$CLUSTER_NAME"; then
  step "Creating kind cluster $CLUSTER_NAME"
  kind create cluster --name "$CLUSTER_NAME" --image "$KIND_NODE_IMAGE" --config env/kind-config.yaml
fi
kubectl config use-context "kind-$CLUSTER_NAME" >/dev/null

step "Loading images into the cluster (nothing is pulled in-cluster)"
env/images.sh

step "Gateway API $GATEWAY_API_VERSION (standard) and Inference Extension $GIE_VERSION CRDs"
kubectl apply --server-side -f \
  "https://github.com/kubernetes-sigs/gateway-api/releases/download/$GATEWAY_API_VERSION/standard-install.yaml"
kubectl apply --server-side -f \
  "https://github.com/kubernetes-sigs/gateway-api-inference-extension/releases/download/$GIE_VERSION/manifests.yaml"

step "InferenceObjective CRD (llm-d-router $LLMD_ROUTER_VERSION)"
kubectl apply --server-side -f \
  "https://raw.githubusercontent.com/llm-d/llm-d-router/$LLMD_ROUTER_VERSION/config/crd/bases/llm-d.ai_inferenceobjectives.yaml"

step "NGINX Gateway Fabric $NGF_VERSION"
helm upgrade --install ngf oci://ghcr.io/nginx/charts/nginx-gateway-fabric \
  --version "$NGF_VERSION" -n nginx-gateway --create-namespace \
  -f env/values/ngf.yaml --wait

step "Kyverno (chart $KYVERNO_CHART_VERSION)"
helm upgrade --install kyverno kyverno --repo https://kyverno.github.io/kyverno \
  --version "$KYVERNO_CHART_VERSION" -n kyverno --create-namespace \
  -f env/values/kyverno.yaml --wait

step "Policy Reporter (chart $POLICY_REPORTER_CHART_VERSION)"
helm upgrade --install policy-reporter policy-reporter --repo https://kyverno.github.io/policy-reporter \
  --version "$POLICY_REPORTER_CHART_VERSION" -n policy-reporter --create-namespace \
  -f env/values/policy-reporter.yaml --wait

step "Platform installed"
