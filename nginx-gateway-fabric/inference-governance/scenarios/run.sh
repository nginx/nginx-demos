#!/usr/bin/env bash
# Runs scenarios. Each scenario is a folder in scenarios/ with a README, its clips (before.sh and
# after.sh; cleanup has run.sh), the manifests they apply, and its policies in policies/.
#
#   scenarios/run.sh                  every scenario in order (make check, with DEMO_AUTO=1)
#   scenarios/run.sh <scenario>       one scenario on its own (make scenario S=<scenario>): runs what
#                                     it builds on off screen, then shows it
#   scenarios/run.sh --demo           the main demo (make demo): see MAIN below
#   scenarios/run.sh --record         record the main demo: <clip>.cast next to each clip (make record)
#
# Some scenarios build on others: Fail-open pools changes the pool Unbudgeted GPU workloads deploys,
# and cleanup reports what the main demo left behind. A scenario lists the ones it builds on in its
# `needs` file; running it on its own runs those first (and what they need), off screen.
#
# With DEMO_AUTO=1 there are no pauses and any outcome that doesn't match what the clip says fails
# the run. Without it, the runner waits for Enter before each clip it shows. Every run starts
# from the cluster `make up` or `make reset` leaves; the runner never resets it (setup/ does, and
# nothing in scenarios/ uses setup/). make scenario and make check S=<scenario> reset first.
set -euo pipefail
cd "$(dirname "$0")"

# Every scenario, in the order of their numbers: the recommended order to roll the policies out.
# The main demo follows one team's escalating workarounds: borrow a model, use the platform's
# large model, run its own, keep serving if the endpoint picker is down; then cleanup. The others
# work and make check runs them, but they weren't in the talk.
ALL=()
for d in [0-9]*-*/; do ALL+=("${d%/}"); done
MAIN=(1-cross-namespace-model-sharing 2-unapproved-model-routing 3-unbudgeted-gpu-workloads
  4-fail-open-pools 8-cleanup)

clips() {   # a scenario's clip scripts, in order
  if [ -f "$1/run.sh" ]; then echo "$1/run"; else echo "$1/before $1/after"; fi
}

needs() {   # what a scenario builds on, recursively (comments and blank lines ignored)
  local d
  [ -f "$1/needs" ] || return 0
  while read -r d; do
    d=${d%%#*} d=${d// /}
    [ -n "$d" ] || continue
    needs "$d"
    echo "$d"
  done <"$1/needs"
}

setup_for() {   # what to run before a scenario: its needs, each once, in story order
  local want s
  want=" $(needs "$1" | tr '\n' ' ') "
  for s in "${ALL[@]}"; do
    if [[ $want == *" $s "* ]]; then echo "$s"; fi
  done
}

demo=0 record=0 only=
case ${1:-} in
  --demo) demo=1 ;;
  --record) demo=1 record=1 ;;
  -*) echo "unknown option $1" >&2; exit 2 ;;
  ?*) only=${1%/}
      if [[ " ${ALL[*]} " != *" $only "* ]]; then
        echo "no scenario '$only'. Scenarios: ${ALL[*]}" >&2; exit 2
      fi ;;
esac
auto=${DEMO_AUTO:-0}
[ "$record" = 1 ] && auto=1   # nobody presses Enter while recording

# PLAN: pairs of <clip> <show|hide>. "hide" runs silently: what it leaves behind is what the
# shown clips start from.
PLAN=()
if [ -n "$only" ]; then
  setup=$(setup_for "$only") list=
  for s in $setup; do list+="${list:+, }$s"; done
  [ -n "$list" ] && printf '\033[2mRunning %s off screen first...\033[0m\n' "$list"
  for s in $setup; do for c in $(clips "$s"); do PLAN+=("$c" hide); done; done
  for c in $(clips "$only"); do PLAN+=("$c" show); done
elif [ "$demo" = 1 ]; then
  for s in "${MAIN[@]}"; do for c in $(clips "$s"); do PLAN+=("$c" show); done; done
else
  for s in "${ALL[@]}"; do for c in $(clips "$s"); do PLAN+=("$c" show); done; done
fi

for ((i = 0; i < ${#PLAN[@]}; i += 2)); do
  clip=${PLAN[i]} mode=${PLAN[i + 1]}
  if [ "$mode" = hide ]; then
    log=$(mktemp)
    if ! DEMO_AUTO=1 "./$clip.sh" >"$log" 2>&1 </dev/null; then
      cat "$log"; rm -f "$log"; exit 1
    fi
    rm -f "$log"
    printf '\033[2m[%s ran off screen]\033[0m\n' "$clip"
    continue
  fi
  if [ "$auto" != 1 ]; then
    clear 2>/dev/null || true
    printf '\033[2mnext: %s (Enter)\033[0m' "$clip"
    read -r -s _ </dev/tty
  fi
  start=$(date +%s)
  if [ "$record" = 1 ]; then
    # Next to the clip: scenarios/<scenario>/before.cast for before.sh.
    ./record.sh "$clip" "$PWD/$clip.cast"
  else
    "./$clip.sh"
  fi
  [ "$auto" = 1 ] && printf '\033[2m[%s took %ss]\033[0m\n' "$clip" "$(( $(date +%s) - start ))"
done
[ "$auto" = 1 ] && printf '\n\033[1;32mCheck passed: every clip behaved as it says.\033[0m\n'
[ "$record" = 1 ] && printf 'Recordings next to each clip. Play them with: make play\n'
exit 0
