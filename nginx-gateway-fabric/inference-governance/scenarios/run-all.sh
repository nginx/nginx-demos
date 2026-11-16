#!/usr/bin/env bash
# Runs the clips in order. Start from `make up` or `make reset`.
#
#   scenarios/run-all.sh           every clip on screen (make check, with DEMO_AUTO=1)
#   scenarios/run-all.sh --demo    the main demo (make demo): see PLAN below
#   scenarios/run-all.sh --record  record the main demo, one asciinema file per clip (make record)
#
# With DEMO_AUTO=1 there are no pauses and any outcome that doesn't match what the clip says fails
# the run. Without it, the runner waits for Enter before each clip it shows.
set -euo pipefail
cd "$(dirname "$0")"

# The main demo: "show" runs on screen; "hide" runs silently in its usual slot (later clips need
# what it leaves behind); "skip" doesn't run. It follows one team's escalating workarounds: borrow
# a model, use the platform's large model, run its own, keep serving if the endpoint picker is down
# (FailOpen). Cut the line, Skip the gateway and Valid config are extra scenarios (make check runs
# them); the Finale expects Cut the line's leftover only when it ran.
PLAN=(
  borrow-a-model/before show   borrow-a-model/after show
  just-point/before show       just-point/after show
  shadow-gpu/before show       shadow-gpu/after show
  cut-the-line/before skip     cut-the-line/after skip
  keep-serving/before show           keep-serving/after show
  skip-the-gateway/before skip skip-the-gateway/after skip
  valid-config/before skip     valid-config/after skip
  finale/run show
)
demo=0 record=0
case ${1:-} in
  --demo) demo=1 ;;
  --record) demo=1 record=1 ;;
esac
auto=${DEMO_AUTO:-0}
[ "$record" = 1 ] && auto=1   # nobody presses Enter while recording
# A recording replaces the whole set: when PLAN changes, clips get new numbers and names, and
# leftovers from an older set would otherwise play back with make play.
[ "$record" = 1 ] && rm -f ../recordings/*.cast

for ((i = 0; i < ${#PLAN[@]}; i += 2)); do
  clip=${PLAN[i]}
  mode=show
  [ "$demo" = 1 ] && mode=${PLAN[i + 1]}
  case $mode in
    skip) continue ;;
    hide)
      log=$(mktemp)
      if ! DEMO_AUTO=1 "./$clip.sh" >"$log" 2>&1 </dev/null; then
        cat "$log"; rm -f "$log"; exit 1
      fi
      rm -f "$log"
      [ "$auto" = 1 ] && printf '\033[2m[%s ran hidden]\033[0m\n' "$clip"
      continue ;;
  esac
  if [ "$auto" != 1 ]; then
    clear 2>/dev/null || true
    printf '\033[2mnext: %s (Enter)\033[0m' "$clip"
    read -r -s _ </dev/tty
  fi
  start=$(date +%s)
  if [ "$record" = 1 ]; then
    # recordings/<n>-<scenario>-<before|after>.cast, numbered in order
    n=$(( ${n:-0} + 1 ))
    mkdir -p ../recordings
    ../tools/record.sh "$clip" "$(cd .. && pwd)/recordings/$(printf %02d "$n")-${clip//\//-}.cast"
  else
    "./$clip.sh"
  fi
  [ "$auto" = 1 ] && printf '\033[2m[%s took %ss]\033[0m\n' "$clip" "$(( $(date +%s) - start ))"
done
[ "$auto" = 1 ] && printf '\n\033[1;32mCheck passed: every clip behaved as it says.\033[0m\n'
[ "$record" = 1 ] && printf 'Recordings in recordings/. Play them with: make play\n'
exit 0
