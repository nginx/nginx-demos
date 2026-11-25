#!/usr/bin/env bash
# "Unapproved model routing", with the policies: P2, the model allowlist, and P3, each team's
# hostnames. P3 goes in off screen: it closes the other way to reach a model the team isn't
# approved for, a route on another team's hostname, and the clip's story is about P2.
source "$(dirname "$0")/../lib.sh"
clip "Unapproved model routing · A list of models per team"

say "Each tenant's list of models, which only the platform can edit"
show "kubectl -n kyverno get configmap model-allowlist -o yaml | yq '.data'"
pause
install_policy scenarios/2-unapproved-model-routing/policies/p2-restrict-model-access.yaml
say "team-assistant switches to the large model again"
expect_denied "not approved for models/llama-70b" "tenant create --dry-run=server -f scenarios/2-unapproved-model-routing/assistant-70b.yaml"
# Off screen: P3, and a check that one team can't take another's hostname with a longer path.
platform apply -f scenarios/2-unapproved-model-routing/policies/p3-restrict-route-hostnames.yaml >/dev/null \
  || fail "couldn't install P3"
wait_for 60 "[ \"\$(kubectl get validatingpolicy restrict-route-hostnames -o jsonpath='{.status.conditionStatus.ready}')\" = true ]"
sleep 3   # webhook config changes take a moment to reach the API server
out=$(research create --dry-run=server -f scenarios/2-unapproved-model-routing/steal-assistant.yaml 2>&1) \
  && fail "P3 let team-research take assistant.example"
grep -q "may not use assistant.example" <<<"$out" || fail "P3 denied the hostname takeover with the wrong message: $out"
pause
say "The routes that already point at llama-70b and research-ft stay: admission doesn't recheck them."
say "The cleanup scenario, at the end, reports them."
pause
