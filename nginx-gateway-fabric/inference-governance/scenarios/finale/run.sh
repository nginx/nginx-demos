#!/usr/bin/env bash
# Finale, "What got in before?"
# Every policy so far blocked the *next* attempt. Everything created before each policy existed
# is still running. Background scans have been checking it all along.
source "$(dirname "$0")/../lib.sh"
clip "Finale · What got in before?"

say "Admission stops the next attempt. What got in before each policy existed?"
# The exact leftovers the earlier clips leave behind: one line per policy and object.
# Wait until every one is reported, then fail on anything missing or extra, so a policy or object
# the background scan silently skips can't pass make check. P1 has background scans off.
EXPECTED="deny-failopen-for-regulated team-assistant/InferencePool/shadow-70b
require-cost-center team-assistant/Deployment/shadow-70b
restrict-gpu-class-by-tier team-assistant/Deployment/shadow-70b
restrict-model-access team-assistant/HTTPRoute/assistant
restrict-model-access team-assistant/HTTPRoute/assistant-pro"
# "Cut the line" isn't in the main demo (make demo), but make check runs it: expect its
# objective only if it exists. Same for the ExternalName fallback (P6), which no clip creates
# since Keep serving became FailOpen only; expect it if someone applied fallback.yaml by hand.
if kubectl -n team-assistant get inferenceobjective assistant-vip >/dev/null 2>&1; then
  EXPECTED+=$'\ncap-objective-priority team-assistant/InferenceObjective/assistant-vip'
fi
if kubectl -n team-assistant get service vendor-fallback >/dev/null 2>&1; then
  EXPECTED+=$'\ninference-backends-only team-assistant/HTTPRoute/assistant-fallback'
  EXPECTED+=$'\ninference-backends-only team-assistant/Service/vendor-fallback'
fi
export LC_ALL=C   # one sort order for sort and comm
reported() {
  kubectl get policyreports -A -o json | jq -r '.items[] | . as $r | .results[]? | select(.result == "fail") |
    ($r.scope // .resources[0]) as $o | .policy + " " + $o.namespace + "/" + $o.kind + "/" + $o.name' | sort -u
}
expected() { printf '%s\n' "$EXPECTED" | sort -u; }
# Waits for the full list: a count could be reached before a slower object was scanned, and the
# Finale would show without it.
for ((i = 0; i < 180; i++)); do
  [ -z "$(comm -23 <(expected) <(reported))" ] && break
  sleep 1
done
missing=$(comm -23 <(expected) <(reported))
extra=$(comm -13 <(expected) <(reported))
[ -z "$missing" ] || fail "background scan never reported: $missing"
[ -z "$extra" ] || fail "background scan reported more than expected: $extra"
# Kyverno writes one PolicyReport per resource, named in .scope; older layouts use results[].resources.
# Each finding is the object and the policy, with Kyverno's message indented below, so it fits
# the recording's 100 columns.
show "kubectl get policyreports -A -o json | jq -r '.items[] | . as \$r | .results[]? | select(.result == \"fail\") |
  (\$r.scope // .resources[0]) as \$o | [(\$o.namespace + \"/\" + \$o.kind + \"/\" + \$o.name), .policy, .message] | @tsv' |
  sort -u | awk -F'\\t' '{printf \"%-42s %s\\n    %s\\n\", \$1, \$2, \$3}'"
say "$(reported | wc -l | tr -d ' ') findings, each with the reason: the to-do list before you switch a policy to Deny."
say "Policy Reporter shows the same reports for the whole cluster (make reporter, http://localhost:8082)."
pause
