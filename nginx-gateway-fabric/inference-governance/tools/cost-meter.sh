#!/usr/bin/env bash
# Simulated GPU spend per model, refreshed every 2s. Run it in a side pane while recording.
#
# Two kinds of (simulated) spend:
#   RESERVED  every running model-server pod costs its GPU's hourly price, used or not.
#   SERVED    chat.sh and flood.sh append the model that answered each request to
#             .cache/ledger.log; each request is priced by the model's GPU class.
# Who pays is the pod's cost-center label. No label, or an external API, shows as UNOWNED.
#   cost-meter.sh          live
#   cost-meter.sh --once   one snapshot
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LEDGER="$ROOT/.cache/ledger.log"
mkdir -p "$ROOT/.cache" && touch "$LEDGER"

declare -A PRICE=([l4]=0.002 [h100]=0.02 [external]=0.03)    # per request
declare -A HOURLY=([l4]=0.80 [h100]=4.00)                      # per GPU-hour, reserved

# model -> "namespace gpu owner", from the model-server pods' labels.
lookup() {
  kubectl get pods -A -l inference.example.com/role=model-server \
    -o jsonpath='{range .items[*]}{.metadata.labels.model-pool} {.metadata.namespace} {.metadata.labels.gpu\.example\.com/class} {.metadata.labels.cost-center}{"\n"}{end}' \
    2>/dev/null | sort -u
}

render() {
  declare -A NS GPU OWNER
  local model ns gpu owner
  while read -r model ns gpu owner; do
    [ -n "$model" ] || continue
    NS[$model]=$ns GPU[$model]=$gpu OWNER[$model]=${owner:-UNOWNED}
  done < <(lookup)

  local total=0 n cost line hourly=0 h
  printf '\033[1mRESERVED GPUs\033[0m\n'
  printf '\033[1m%-22s %-16s %-9s %-16s %14s\033[0m\n' MODEL NAMESPACE GPU COST-CENTER '$/HOUR'
  for model in $(printf '%s\n' "${!NS[@]}" | sort); do
    gpu=${GPU[$model]} owner=${OWNER[$model]}
    h=${HOURLY[$gpu]:-0}
    hourly=$(awk -v a="$hourly" -v b="$h" 'BEGIN {printf "%.2f", a + b}')
    line=$(printf '%-22s %-16s %-9s %-16s %14s' "$model" "${NS[$model]}" "$gpu" "$owner" "\$$h")
    if [ "$owner" = UNOWNED ]; then printf '\033[1;31m%s\033[0m\n' "$line"; else printf '%s\n' "$line"; fi
  done
  printf '\033[1m%-66s %14s\033[0m\n\n' 'RESERVED TOTAL' "\$$hourly/h"
  printf '\033[1mSERVED REQUESTS\033[0m\n'
  printf '\033[1m%-22s %-16s %-9s %-16s %8s %9s\033[0m\n' MODEL NAMESPACE GPU COST-CENTER REQUESTS SPEND
  while read -r n model; do
    [ -n "$model" ] || continue
    ns=${NS[$model]:-external} gpu=${GPU[$model]:-external} owner=${OWNER[$model]:-UNOWNED}
    cost=$(awk -v n="$n" -v p="${PRICE[$gpu]:-0}" 'BEGIN {printf "%.2f", n * p}')
    total=$(awk -v a="$total" -v b="$cost" 'BEGIN {printf "%.2f", a + b}')
    line=$(printf '%-22s %-16s %-9s %-16s %8s %9s' "$model" "$ns" "$gpu" "$owner" "$n" "\$$cost")
    if [ "$owner" = UNOWNED ]; then printf '\033[1;31m%s\033[0m\n' "$line"; else printf '%s\n' "$line"; fi
  done < <(awk '{print $2}' "$LEDGER" | sort | uniq -c | sort -rn)
  printf '\033[1m%-75s %9s\033[0m\n' 'SERVED TOTAL' "\$$total"
  printf '\033[2msimulated prices: l4 $%s/h, h100 $%s/h; per request l4 $%s, h100 $%s, external API $%s\033[0m\n' \
    "${HOURLY[l4]}" "${HOURLY[h100]}" "${PRICE[l4]}" "${PRICE[h100]}" "${PRICE[external]}"
}

if [ "${1:-}" = --once ]; then
  render
  exit 0
fi

while true; do
  out=$(render)
  clear
  printf '\033[1;34mGPU spend (simulated)\033[0m\n\n%s\n' "$out"
  sleep 2
done
