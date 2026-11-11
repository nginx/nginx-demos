#!/usr/bin/env bash
# "Keep serving", with the policy: P7, regulated tenants can't fail open.
source "$(dirname "$0")/../lib.sh"
clip "Keep serving · Regulated teams fail closed"

say "Regulated tenants fail closed: no InferencePool in a regulated namespace may set FailOpen."
install_policy p7-deny-failopen-for-regulated
pause
say "team-assistant sets FailOpen again"
expect_denied "FailOpen is not allowed" \
  "tenant create --dry-run=server -f scenarios/keep-serving/failopen-pool.yaml"
pause
