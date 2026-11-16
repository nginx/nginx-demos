#!/usr/bin/env bash
# "Cut the line", with the policy: P5 priority capped by tier.
source "$(dirname "$0")/../lib.sh"
clip "Cut the line · Priority capped by tier"

say "Standard tier gets priority 0, premium up to 10"
show "yq '.spec.variables, .spec.validations' policies/p5-cap-objective-priority/policy.yaml"
install_policy p5-cap-objective-priority
pause
expect_denied "max priority is 0" "tenant create --dry-run=server -f scenarios/cut-the-line/objective.yaml"
pause
