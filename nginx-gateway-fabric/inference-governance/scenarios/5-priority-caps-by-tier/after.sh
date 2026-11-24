#!/usr/bin/env bash
# "Priority caps by tier", with the policy: P8 priority capped by tier.
source "$(dirname "$0")/../lib.sh"
clip "Priority caps by tier · Standard tier gets priority 0"

say "Standard tier gets priority 0, premium up to 10"
show "yq '.spec.variables, .spec.validations' scenarios/5-priority-caps-by-tier/policies/p8-cap-objective-priority.yaml"
install_policy scenarios/5-priority-caps-by-tier/policies/p8-cap-objective-priority.yaml
pause
expect_denied "max priority is 0" "tenant create --dry-run=server -f scenarios/5-priority-caps-by-tier/objective.yaml"
pause
