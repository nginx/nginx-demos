#!/usr/bin/env bash
# "Keep serving", before the policy
# Assistant Pro is customer-facing, so team-assistant wants it never to show an error. If the
# endpoint picker is down, an InferencePool with failureMode FailClose (the default) refuses
# requests; with FailOpen, NGF sends them straight to the pool's pods, skipping the picker's
# scheduling. team-assistant owns the pool it deployed in "Shadow GPU" (shadow-70b), so its role
# lets it change that. The platform's rule: regulated tenants fail closed (P7).
# (There's also an ExternalName fallback, fallback.yaml, and P6; no clip creates it.)
source "$(dirname "$0")/../lib.sh"
clip "Keep serving"

say "Assistant Pro is customer-facing: customers must never see an error. team-assistant's role lets it edit its own InferencePool"
expect_ok "kubectl auth can-i patch inferencepools -n team-assistant --as team-assistant-dev"
say "Today its pool fails closed: if the endpoint picker is down, requests get an error"
show "tenant -n team-assistant get inferencepool shadow-70b -o jsonpath='{.spec.endpointPickerRef.failureMode}{\"\\n\"}'"
pause
say "So: if the picker goes down, keep serving anyway"
# P3 is installed since "Shadow GPU" and checks every update, so the pool needs its owner first.
# That's setup, so it runs off screen.
tenant -n team-assistant label inferencepool shadow-70b cost-center=team-assistant --overwrite >/dev/null \
  || fail "couldn't label the shadow-70b pool"
expect_ok "tenant -n team-assistant patch inferencepool shadow-70b --type=merge -p '{\"spec\":{\"endpointPickerRef\":{\"failureMode\":\"FailOpen\"}}}'"
show "tenant -n team-assistant get inferencepool shadow-70b -o jsonpath='{.spec.endpointPickerRef.failureMode}{\"\\n\"}'"
say "RBAC can't see that it switched off the platform's failure mode. team-assistant is regulated: without the picker, requests skip its scheduling."
pause
