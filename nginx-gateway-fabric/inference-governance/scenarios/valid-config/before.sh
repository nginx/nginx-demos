#!/usr/bin/env bash
# "Valid config, bad request", before the policy
# Every object passes every policy now. A load test from team-assistant floods the shared llama-8b
# pool through a perfectly valid route.
source "$(dirname "$0")/../lib.sh"
clip "Valid config, bad request"

say "team-assistant's customer assistant on a fresh route (assistant-v2.example): small model, passes every policy"
say "A load test: 10 requests a second for 10 seconds"
expect_ok "tools/flood.sh assistant-v2.example 10 10 | tee .cache/flood-before.txt"
grep -q "HTTP 429" .cache/flood-before.txt && fail "flood got 429s before any rate limit existed"
say "Admission control sees objects, not requests. Every one of these was allowed."
pause
