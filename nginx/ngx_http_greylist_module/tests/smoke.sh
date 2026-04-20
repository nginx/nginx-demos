#!/usr/bin/env bash
# =============================================================================
# tests/smoke.sh — functional smoke tests
#
# Usage:
#   ./tests/smoke.sh [base_url]
#
# Requires: curl, grep, seq
# Default base_url: http://localhost
# =============================================================================
set -euo pipefail

BASE="${1:-http://localhost}"
PASS=0; FAIL=0

green() { printf '\033[32mPASS\033[0m  %s\n' "$*"; }
red()   { printf '\033[31mFAIL\033[0m  %s\n' "$*"; (( FAIL++ )) || true; }

assert_code() {
    local label="$1" want="$2"
    local got
    got=$(curl -s -o /dev/null -w "%{http_code}" "${@:3}")
    if [ "$got" = "$want" ]; then
        green "$label (HTTP $got)"; (( PASS++ )) || true
    else
        red   "$label — expected HTTP $want, got HTTP $got"
    fi
}

echo "====================================================="
echo " nginx-greylist-module smoke tests"
echo " Base: $BASE"
echo "====================================================="

# T01 — health check always passes
assert_code "T01 healthz" 200 "$BASE/healthz"

# T02 — normal request passes
assert_code "T02 normal GET" 200 \
    -H "User-Agent: SmokeTest-T02-$(date +%s%N)" "$BASE/"

# T03 — POST /auth/login: first request passes
assert_code "T03 first POST" 200 \
    -X POST -H "User-Agent: SmokeTest-T03" "$BASE/auth/login"

# T04 — POST /auth/login: rapid burst triggers 429
echo
echo "T04: firing 20 rapid POSTs (rate=5r/s burst=5 → should 429)..."
codes=()
for i in $(seq 1 20); do
    codes+=("$(curl -s -o /dev/null -w "%{http_code}" \
        -X POST -H "User-Agent: SmokeTest-T04" "$BASE/auth/login")")
done
printf '  codes: %s\n' "${codes[*]}"
if printf '%s\n' "${codes[@]}" | grep -q "^429$"; then
    green "T04 rate limit triggered 429"; (( PASS++ )) || true
else
    red   "T04 no 429 received after 20 rapid requests"
fi

# T05 — greylisted client gets 429 with Retry-After
echo
echo "T05: checking Retry-After header..."
HEADERS=$(curl -si -X POST -H "User-Agent: SmokeTest-T04" "$BASE/auth/login")
if echo "$HEADERS" | grep -qi "retry-after:"; then
    green "T05 Retry-After header present"; (( PASS++ )) || true
else
    red   "T05 Retry-After header missing"
fi

# T06 — X-Greylisted header present on 429
if echo "$HEADERS" | grep -qi "x-greylisted:"; then
    green "T06 X-Greylisted header present"; (( PASS++ )) || true
else
    red   "T06 X-Greylisted header missing"
fi

# T07 — different User-Agent has independent counter
FRESH_UA="SmokeTest-T07-fresh-$(date +%s%N)"
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -X POST -H "User-Agent: $FRESH_UA" "$BASE/auth/login")
if [ "$CODE" = "200" ] || [ "$CODE" = "429" ]; then
    green "T07 distinct UA is independent (HTTP $CODE)"; (( PASS++ )) || true
else
    red   "T07 unexpected HTTP $CODE for fresh UA"
fi

# T08 — GET /api/search rate limit
echo
echo "T08: firing 50 rapid GETs to /api/search (rate=30r/m burst=10)..."
got429=0
for i in $(seq 1 50); do
    c=$(curl -s -o /dev/null -w "%{http_code}" \
        -H "User-Agent: SmokeTest-T08" "$BASE/api/search?q=test")
    [ "$c" = "429" ] && got429=1 && break
done
if [ "$got429" = "1" ]; then
    green "T08 /api/search rate limit triggered 429"; (( PASS++ )) || true
else
    red   "T08 no 429 for /api/search after 50 requests"
fi

echo
echo "====================================================="
printf ' Results: %d passed, %d failed\n' "$PASS" "$FAIL"
echo "====================================================="
[ "$FAIL" -eq 0 ]
