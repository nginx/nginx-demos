#!/usr/bin/env bash
# What a customer of <host> sees. Sends one prompt through the gateway and prints the answer and
# which model produced it.
#   chat.sh <host> [prompt] [extra curl args...]
set -euo pipefail
source "$(dirname "$0")/../versions.env"

host=$1
prompt=${2:-"How do I reset my password?"}
shift $(( $# >= 2 ? 2 : 1 ))

body=$(jq -nc --arg p "$prompt" '{model: "auto", max_tokens: 60, messages: [{role: "user", content: $p}]}')
resp=$(curl -sS -m 20 -w '\n%{http_code}' "$GATEWAY_URL/v1/chat/completions" \
  -H "Host: $host" -H 'Content-Type: application/json' "$@" -d "$body") || true
code=${resp##*$'\n'}
json=${resp%$'\n'*}

printf '\033[2mcustomer@%s>\033[0m %s\n' "$host" "$prompt"
if [ "$code" != 200 ]; then
  printf '\033[31m[HTTP %s]\033[0m %s\n' "$code" "$(head -c 200 <<<"$json")"
  exit 0
fi
model=$(jq -r '.model // "?"' <<<"$json")
ledger="$(dirname "$0")/../.cache/ledger.log"
mkdir -p "$(dirname "$ledger")" && printf '%s %s\n' "$(date +%s)" "$model" >>"$ledger"
answer=$(jq -r '.choices[0].message.content // ""' <<<"$json")
if [[ $answer == INTERNAL:* ]]; then
  printf '\033[1;31m%s\033[0m  \033[2m(model: %s)\033[0m\n' "$answer" "$model"
else
  printf '%s  \033[2m(model: %s)\033[0m\n' "$answer" "$model"
fi
