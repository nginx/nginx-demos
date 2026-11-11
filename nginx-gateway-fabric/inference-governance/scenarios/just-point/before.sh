#!/usr/bin/env bash
# "Just point at the big one", before the policy
# Borrowing is locked down, so team-assistant looks at the platform's shared models. The platform
# offers them to tenants through one ReferenceGrant (env/manifests/10-gateway.yaml); which team may
# use which is a separate list (the model allowlist), and nothing checks a route against it yet.
source "$(dirname "$0")/../lib.sh"
clip "Just point at the big one"

say "Assistant Pro still needs a better model. team-assistant's role lets it create and edit HTTPRoutes in its own namespace"
expect_ok "kubectl auth can-i create httproutes -n team-assistant --as team-assistant-dev"
say "RBAC can't see which pool a route points at. The platform offers its models to tenants through one ReferenceGrant; which team may use which is on its approval list:"
show "tenant get inferencepools -n models -L model,cost-center"
pause
say "So team-assistant upgrades its assistant route: one line"
show "diff -u env/manifests/30-routes.yaml scenarios/just-point/assistant-70b.yaml | grep -E '^[-+] ' || true"
expect_ok "tenant apply -f scenarios/just-point/assistant-70b.yaml"
pause
wait_for 30 "tools/chat.sh assistant.example | grep -q 'model: llama-70b'"
show "tools/chat.sh assistant.example"
say "Every customer now gets the large model on top-end GPUs. The approval list says llama-8b, and nothing checked it."
pause
