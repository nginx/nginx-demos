#!/usr/bin/env bash
# A "load test" from a valid route: <rps> requests per second for <seconds>, then a status-code
# histogram. Always exits 0; callers inspect the output.
#   flood.sh <host> <rps> <seconds>
set -uo pipefail
source "$(dirname "$0")/../versions.env"

host=$1 rps=$2 secs=$3
body='{"model":"auto","max_tokens":8,"messages":[{"role":"user","content":"ping"}]}'
tmp=$(mktemp)
trap 'rm -f "$tmp" "$tmp".*' EXIT

for ((s = 0; s < secs; s++)); do
  for ((r = 0; r < rps; r++)); do
    curl -s -o "$tmp.$s.$r" -m 10 -w '%{http_code}\n' "$GATEWAY_URL/v1/chat/completions" \
      -H "Host: $host" -H 'Content-Type: application/json' -d "$body" >>"$tmp" &
  done
  sleep 1
done
wait

# Feed the cost meter: one ledger line per answered request, named by the model that answered.
ledger="$(dirname "$0")/../.cache/ledger.log"
mkdir -p "$(dirname "$ledger")"
for f in "$tmp".*; do
  m=$(jq -r '.model // empty' "$f" 2>/dev/null) && [ -n "$m" ] && printf '%s %s\n' "$(date +%s)" "$m" >>"$ledger"
  rm -f "$f"
done

total=$(wc -l <"$tmp" | tr -d ' ')
printf 'sent %s requests to %s in %ss\n' "$total" "$host" "$secs"
sort "$tmp" | uniq -c | while read -r n code; do
  case $code in
    200) color='\033[32m' ;;
    429) color='\033[1;33m' ;;
    *) color='\033[31m' ;;
  esac
  printf "  ${color}HTTP %s\033[0m  x %s\n" "$code" "$n"
done
