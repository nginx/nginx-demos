#!/usr/bin/env bash
# "Unbudgeted GPU workloads", with the policies: P4 require cost-center, P5 h100 only for premium
# tier.
source "$(dirname "$0")/../lib.sh"
clip "Unbudgeted GPU workloads · Who pays, and on which GPUs"

say "Everything that holds a GPU names who pays, and standard tier stays off h100, the expensive class."
install_policy scenarios/3-unbudgeted-gpu-workloads/policies/p4-require-cost-center.yaml
install_policy scenarios/3-unbudgeted-gpu-workloads/policies/p5-restrict-gpu-class-by-tier.yaml
pause
say "team-assistant redeploys the same model"
expect_denied "needs a cost-center label" \
  "tenant create --dry-run=server -f scenarios/3-unbudgeted-gpu-workloads/shadow-70b.yaml"
pause
say "...adds a cost-center label and tries again"
expect_denied "can't use h100 GPUs" \
  "yq '(select(.kind == \"Deployment\" or .kind == \"InferencePool\") | .metadata.labels.cost-center) = \"team-assistant\" | (select(.kind == \"Deployment\") | .spec.template.metadata.labels.cost-center) = \"team-assistant\"' scenarios/3-unbudgeted-gpu-workloads/shadow-70b.yaml | tenant create --dry-run=server -f -"
pause
