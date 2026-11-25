#!/usr/bin/env bash
# Plays the recorded clips (make record) as if live: each clip pauses at every beat. A clip's
# recording is next to its script: scenarios/<scenario>/before.cast for before.sh.
#
#   scenarios/play.sh               the main demo's recordings, in order (make play)
#   scenarios/play.sh <scenario>    one scenario's (make play S=4-fail-open-pools)
#
# The recordings are the talk's, so they show the scenarios' names at the time.
#
# Before each clip a start screen shows its title and the playback keys, and waits for Enter.
# Keys: Space continues (and pauses), ']' skips to the next beat while paused, '.' steps one frame
# while paused, Ctrl-C ends a clip. The terminal must be at least as big as the recording
# (RECORD_SIZE, 100x28): playback replays the original output, it doesn't rescale.
set -euo pipefail
cd "$(dirname "$0")/.."

# The main demo's scenarios, in order, and each one's clips: from the runner, so the order here
# is the order make demo shows them.
eval "$(sed -n '/^MAIN=(/,/)$/p;/^clips() {/,/^}/p' scenarios/run.sh)"
if [ -n "${1:-}" ]; then
  scenarios=("${1%/}")
  [ -d "scenarios/${scenarios[0]}" ] || { echo "no scenario '$1'. Scenarios: ${MAIN[*]}" >&2; exit 1; }
else
  scenarios=("${MAIN[@]}")
fi
casts=()
for s in "${scenarios[@]}"; do
  for c in $(cd scenarios && clips "$s"); do
    [ -f "scenarios/$c.cast" ] && casts+=("scenarios/$c.cast")
  done
done
[ ${#casts[@]} -gt 0 ] || { echo "no recordings for ${scenarios[*]}; make record records the main demo" >&2; exit 1; }

# Playback reads Space and Enter, and needs the real window size: refuse anything but a terminal.
if [ ! -t 0 ] || [ ! -t 1 ]; then
  echo "make play needs a real terminal (it reads Space and Enter): run it in Terminal or iTerm." >&2
  exit 1
fi
# Refuse a terminal smaller than the recordings: wrapped lines would scramble the screen.
read -r cols rows < <(python3 -c 'import json, sys; h = json.loads(open(sys.argv[1]).readline()); t = h.get("term", h); print(t.get("cols", t.get("width")), t.get("rows", t.get("height")))' "${casts[0]}")
if [ "$(tput cols)" -lt "$cols" ] || [ "$(tput lines)" -lt "$rows" ]; then
  echo "terminal is $(tput cols)x$(tput lines); the recordings need ${cols}x${rows}. Enlarge the window (or shrink the font) and retry." >&2
  exit 1
fi

# The clip's title: the header line it prints first ("── Keep serving ──" in the talk's recordings).
title() {
  python3 - "$1" <<'PY'
import json, re, sys
lines = open(sys.argv[1]).read().splitlines()[1:60]
text = "".join(e[2] for e in map(json.loads, lines) if e[1] == "o")
m = re.search(r"── (.+?) ──", re.sub(r"\x1b\[[0-9;]*m", "", text))
print(m.group(1) if m else "")
PY
}

total=${#casts[@]} n=0
for cast in "${casts[@]}"; do
  n=$((n + 1))
  scenario=${cast#scenarios/} scenario=${scenario%%/*}
  clear
  printf '\n  \033[1m%s / %s  ·  %s\033[0m\n' "$n" "$total" "$(title "$cast")"
  printf '  \033[2m%s  ·  make play S=%s\033[0m\n\n' "$cast" "$scenario"
  printf '  Space      pause and continue (the clip stops at every beat)\n'
  printf '  ]          while paused: skip to the next beat\n'
  printf '  .          while paused: one step forward\n'
  printf '  Ctrl-C     end the clip\n\n'
  printf '  \033[1mEnter to start\033[0m'
  read -r -s _ </dev/tty
  # The recordings cap waits at 2 s (a policy becoming Ready, the cleanup scenario's background
  # scan); 1 s keeps a still screen short and still leaves the 0.5 s beat after each Space.
  asciinema play --quiet --pause-on-markers --idle-time-limit 1 "$cast" || true
done
