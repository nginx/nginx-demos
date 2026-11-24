#!/usr/bin/env bash
# "Unapproved model routing", before the policy
# Sharing models is the platform's now (Cross-namespace model sharing), so team-assistant looks at
# the platform's own shared models. The platform offers them to tenants through one ReferenceGrant
# (setup/workloads/manifests/10-gateway.yaml); which team may use which is a separate list (the
# model allowlist), and nothing checks a route against it yet.
source "$(dirname "$0")/../lib.sh"
clip "Unapproved model routing"

say "Assistant Pro still needs a better model. team-assistant's role lets it create and edit HTTPRoutes in its own namespace"
expect_ok "kubectl auth can-i create httproutes -n team-assistant --as team-assistant-dev"
say "RBAC can't see which pool a route points at. The platform offers its models to tenants through one ReferenceGrant; which team may use which is on its approval list:"
show "tenant get inferencepools -n models -L model,cost-center"
pause
say "So team-assistant upgrades its assistant route: one line"
# kubectl diff compares the file with the route in the cluster; only the changed lines are shown.
show "tenant diff -f scenarios/2-unapproved-model-routing/assistant-70b.yaml | grep -E '^[-+] ' | grep -v generation || true"
expect_ok "tenant apply -f scenarios/2-unapproved-model-routing/assistant-70b.yaml"
pause
wait_for 30 "chat.sh assistant.example | grep -q 'model: llama-70b'"
show "chat.sh assistant.example"
say "Every customer now gets the large model on top-end GPUs. The approval list says llama-8b, and nothing checked it."
pause
