#!/usr/bin/env bash
# "Fail-open pools", with the policy: P6, regulated tenants can't fail open.
source "$(dirname "$0")/../lib.sh"
clip "Fail-open pools · Regulated teams fail closed"

say "Regulated tenants fail closed: no InferencePool in a regulated namespace may set FailOpen."
install_policy scenarios/4-fail-open-pools/policies/p6-deny-failopen-for-regulated.yaml
pause
say "team-assistant sets FailOpen again"
expect_denied "FailOpen is not allowed" \
  "tenant create --dry-run=server -f scenarios/4-fail-open-pools/failopen-pool.yaml"
pause
