#!/usr/bin/env bash
# "Shadow GPU", before the policies
# Can't route to llama-70b, can't borrow research-ft. So team-assistant runs its own 70B on h100s,
# inside its own namespace. Nothing says who pays for it.
source "$(dirname "$0")/../lib.sh"
clip "Shadow GPU"

say "Assistant Pro still needs a better model. team-assistant's role lets it run its own: Deployments and InferencePools in its own namespace"
expect_ok "kubectl auth can-i create deployments -n team-assistant --as team-assistant-dev"
say "RBAC can't see which GPUs a Deployment asks for, or who pays. team-assistant runs its own copy of the large model:"
# Just the model server's and the pool's labels: GPU class, and no cost-center anywhere.
show "yq 'select(.kind == \"InferencePool\" or .metadata.name == \"shadow-70b\") | {\"kind\": .kind, \"name\": .metadata.name, \"labels\": (.spec.template.metadata.labels // .metadata.labels)}' scenarios/shadow-gpu/shadow-70b.yaml"
pause
expect_ok "tenant apply -f scenarios/shadow-gpu/shadow-70b.yaml"
show "tenant -n team-assistant rollout status deploy/shadow-70b --timeout=120s"
pause
say "It runs on an h100, the top-end GPU class, which costs money whether anyone calls it or not."
show "tools/cost-meter.sh --once | sed -n '1,/RESERVED TOTAL/p'"
say "shadow-70b has no cost-center, so nobody is billed for it."
pause
