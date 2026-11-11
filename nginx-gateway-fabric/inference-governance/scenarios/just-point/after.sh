#!/usr/bin/env bash
# "Just point at the big one", with the policy: P2, the model allowlist.
source "$(dirname "$0")/../lib.sh"
clip "Just point at the big one · A list of models per team"

say "Each tenant's list of models, which only the platform can edit"
show "kubectl -n kyverno get configmap model-allowlist -o yaml | yq '.data'"
pause
install_policy p2-restrict-model-access
say "team-assistant switches to the large model again"
expect_denied "not approved for models/llama-70b" "tenant create --dry-run=server -f scenarios/just-point/assistant-70b.yaml"
pause
say "The routes that already point at llama-70b and research-ft stay: admission doesn't recheck them."
say "The Finale comes back to them."
pause
