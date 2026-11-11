#!/usr/bin/env bash
# "Cut the line", before the policy
# Priority is part of what a tier buys: when a pool is saturated, higher-priority requests are
# served first. InferenceObjective sets it, and tenants can write their own at any value.
# An objective lives in its pool's namespace, so here it's team-assistant's own pool (shadow-70b): this
# shows the tier contract being ignored, not one tenant overtaking another on a shared pool.
source "$(dirname "$0")/../lib.sh"
clip "Cut the line"

say "Priority is part of the tier: standard gets 0. InferenceObjective sets it, and tenants write their own."
show "cat scenarios/cut-the-line/objective.yaml"
pause
expect_ok "tenant apply -f scenarios/cut-the-line/objective.yaml"
show "tenant -n team-assistant get inferenceobjectives"
say "Standard tier, priority 100. Nothing checked the number against the tier."
pause
