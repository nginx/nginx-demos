#!/usr/bin/env bash
# Puts the cluster back to the starting state (about 1 minute). Removes demo
# policies, their reports and generated objects, everything team-assistant created, then
# re-applies the baseline and restarts the model servers so request counters start at zero.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
source versions.env

step() { printf '\033[1;34m==> %s\033[0m\n' "$*"; }

step "Removing demo policies and what they generated"
kubectl delete validatingpolicies,generatingpolicies,deletingpolicies --all --ignore-not-found --wait
kubectl delete validatingadmissionpolicies,validatingadmissionpolicybindings \
  -l app.kubernetes.io/managed-by=kyverno --ignore-not-found
kubectl delete networkpolicies,ratelimitpolicies -A -l app.kubernetes.io/managed-by=kyverno --ignore-not-found
kubectl delete policyreports -A --all --ignore-not-found >/dev/null
kubectl delete -f policies/rbac/ --ignore-not-found

step "Removing everything created during the scenarios"
kubectl -n team-assistant delete httproutes,inferencepools,inferenceobjectives,deployments,services,pods,referencegrants \
  --all --ignore-not-found --wait=false
# Some of the chart's objects carry no labels, so select by name: everything except what
# Kubernetes creates (default ServiceAccount, kube-root-ca.crt), the baseline RoleBinding
# (env/manifests/01-rbac.yaml) and the datasets re-applied below.
kubectl -n team-assistant get roles,rolebindings,serviceaccounts,configmaps -o name \
  | grep -vE '^(serviceaccount/default|configmap/kube-root-ca\.crt|configmap/dataset-.*|rolebinding\.rbac\.authorization\.k8s\.io/team-assistant-dev)$' \
  | xargs -r kubectl -n team-assistant delete --ignore-not-found
kubectl -n team-research delete referencegrants --all --ignore-not-found
kubectl -n models patch inferencepool llama-70b --type=merge \
  -p '{"spec":{"endpointPickerRef":{"failureMode":"FailClose"}}}' >/dev/null 2>&1 || true

step "Re-applying the baseline"
kubectl apply -f env/manifests/00-namespaces.yaml -f env/manifests/01-rbac.yaml -f env/manifests/10-gateway.yaml \
  -f env/manifests/20-allowlist.yaml -f env/manifests/30-routes.yaml >/dev/null
for ds in general research-internal vendor; do
  kubectl -n team-assistant create configmap "dataset-$ds" --from-file=dataset.sqlite3=".cache/datasets/$ds.sqlite3" \
    --dry-run=client -o yaml | kubectl apply -f - >/dev/null
done

step "Zeroing the cost meter"
: > .cache/ledger.log
kubectl -n models scale deploy llama-8b-epp llama-70b-epp --replicas=1 >/dev/null
kubectl -n team-assistant wait pod --all --for=delete --timeout=60s >/dev/null 2>&1 || true

tools/verify.sh
