#!/usr/bin/env bash
# "Valid config, bad request", with the policy: G2 generates a per-tenant rate limit.
source "$(dirname "$0")/../lib.sh"
clip "Valid config, bad request · A rate limit for every route"

say "Admission can't count requests. So generate the gateway's rate limit for every tenant route to a model, sized by tier"
show "yq '.spec.variables[] | select(.name == \"rate\" or .name == \"burst\")' policies/g2-tenant-rate-limit/policy.yaml"
install_policy g2-tenant-rate-limit
wait_for 60 "kubectl -n team-assistant get ratelimitpolicy assistant-v2-tenant-rate-limit"
show "kubectl -n team-assistant get ratelimitpolicy assistant-v2-tenant-rate-limit -o yaml | yq '.spec'"
pause
sleep 5   # let NGF push the new config
say "The same load test"
expect_ok "tools/flood.sh assistant-v2.example 10 10 | tee .cache/flood-after.txt"
grep -q "HTTP 429" .cache/flood-after.txt || fail "flood got no 429s with the rate limit in place"
pause
say "team-assistant deletes it"
OLD_UID=$(kubectl -n team-assistant get ratelimitpolicy assistant-v2-tenant-rate-limit -o jsonpath='{.metadata.uid}')
export OLD_UID
expect_ok "tenant -n team-assistant delete ratelimitpolicy assistant-v2-tenant-rate-limit"
wait_for 60 "[ -n \"\$(kubectl -n team-assistant get ratelimitpolicy assistant-v2-tenant-rate-limit -o jsonpath='{.metadata.uid}' 2>/dev/null)\" ] && [ \"\$(kubectl -n team-assistant get ratelimitpolicy assistant-v2-tenant-rate-limit -o jsonpath='{.metadata.uid}')\" != \"\$OLD_UID\" ]"
show "tenant -n team-assistant get ratelimitpolicy"
say "Kyverno put it back."
pause
