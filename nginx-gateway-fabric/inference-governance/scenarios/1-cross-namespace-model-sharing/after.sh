#!/usr/bin/env bash
# "Cross-namespace model sharing", with the policy: P1, only the platform shares InferencePools.
source "$(dirname "$0")/../lib.sh"
clip "Cross-namespace model sharing · Only the platform shares models"

say "Only the platform team may create ReferenceGrants that share an InferencePool"
show "yq '.spec.validations' scenarios/1-cross-namespace-model-sharing/policies/p1-restrict-inferencepool-referencegrants.yaml"
install_policy scenarios/1-cross-namespace-model-sharing/policies/p1-restrict-inferencepool-referencegrants.yaml
pause
say "Plain CEL, so Kyverno also generates a native ValidatingAdmissionPolicy: the API server enforces it directly"
wait_for 30 "kubectl get validatingadmissionpolicy -o name | grep -q restrict-inferencepool-referencegrants"
show "kubectl get validatingadmissionpolicies,validatingadmissionpolicybindings -o custom-columns=KIND:.kind,NAME:.metadata.name | grep -E 'KIND|restrict-inferencepool'"
pause
say "team-research tries to share its model again"
expect_denied "only the platform team can share InferencePools" \
  "research create --dry-run=server -f scenarios/1-cross-namespace-model-sharing/grant.yaml"
pause
