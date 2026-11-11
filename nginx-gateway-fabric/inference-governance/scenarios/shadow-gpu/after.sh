#!/usr/bin/env bash
# "Shadow GPU", with the policies: P3 require cost-center, P4 h100 only for premium tier.
source "$(dirname "$0")/../lib.sh"
clip "Shadow GPU · Who pays, and on which GPUs"

say "Everything that holds a GPU names who pays, and standard tier stays off h100, the expensive class."
install_policy p3-require-cost-center
install_policy p4-restrict-gpu-class-by-tier
pause
say "team-assistant redeploys the same model"
expect_denied "needs a cost-center label" \
  "tenant create --dry-run=server -f scenarios/shadow-gpu/shadow-70b.yaml"
pause
say "...adds a cost-center label and tries again"
expect_denied "can't use h100 GPUs" \
  "yq '(select(.kind == \"Deployment\" or .kind == \"InferencePool\") | .metadata.labels.cost-center) = \"team-assistant\" | (select(.kind == \"Deployment\") | .spec.template.metadata.labels.cost-center) = \"team-assistant\"' scenarios/shadow-gpu/shadow-70b.yaml | tenant create --dry-run=server -f -"
pause
