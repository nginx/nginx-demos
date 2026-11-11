#!/usr/bin/env bash
# Plays the recorded clips (make record) as if live: each clip pauses at every beat.
#
#   tools/play.sh                              every recording, in order (make play)
#   tools/play.sh keep-serving                 recordings whose name contains keep-serving
#                                              (make play CLIP=keep-serving)
#   tools/play.sh --clip 7                     one recording by its number (make play-7)
#   tools/play.sh --clip keep-serving-before   or by name (make play-keep-serving-before): the clip's
#                                              script path, scenarios/keep-serving/before.sh, with dashes
#
# Before each clip a start screen shows its number, its title and the playback keys, and waits
# for Enter. Keys: Space continues (and pauses), ']' skips to the next beat while paused, '.'
# steps one frame while paused, Ctrl-C ends a clip. The terminal must be at least as big as the
# recording (RECORD_SIZE, 100x28): playback replays the original output, it doesn't rescale.
set -euo pipefail
cd "$(dirname "$0")/.."

shopt -s nullglob
if [ "${1:-}" = --clip ]; then
  clip=${2:-}
  [[ $clip =~ ^[0-9]$ ]] && clip=0$clip
  if [[ $clip =~ ^[0-9][0-9]$ ]]; then
    casts=(recordings/"$clip"-*.cast)
  else
    casts=(recordings/[0-9][0-9]-"$clip".cast)
  fi
  if [ ${#casts[@]} -eq 0 ]; then
    echo "no recording '${2:-}'. These are recorded (make record makes them):" >&2
    for c in recordings/[0-9][0-9]-*.cast; do
      c=${c##*/} && c=${c%.cast}
      printf '  make play-%-3s or make play-%s\n' "$((10#${c%%-*}))" "${c#[0-9][0-9]-}" >&2
    done
    exit 1
  fi
else
  casts=(recordings/*"${1:-}"*.cast)
  [ ${#casts[@]} -gt 0 ] || { echo "no recordings matching recordings/*${1:-}*.cast; run make record" >&2; exit 1; }
fi

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

# The clip's title: the header line it prints first ("── Keep serving ──").
title() {
  python3 - "$1" <<'PY'
import json, re, sys
lines = open(sys.argv[1]).read().splitlines()[1:60]
text = "".join(e[2] for e in map(json.loads, lines) if e[1] == "o")
m = re.search(r"── (.+?) ──", re.sub(r"\x1b\[[0-9;]*m", "", text))
print(m.group(1) if m else "")
PY
}

all=(recordings/[0-9][0-9]-*.cast)
total=${#all[@]}
for cast in "${casts[@]}"; do
  name=$(basename "$cast" .cast)
  n=$((10#${name%%-*}))
  clear
  printf '\n  \033[1m%s / %s  ·  %s\033[0m\n' "$n" "$total" "$(title "$cast")"
  printf '  \033[2mmake play-%s  ·  make play-%s\033[0m\n\n' "$n" "${name#[0-9][0-9]-}"
  printf '  Space      pause and continue (the clip stops at every beat)\n'
  printf '  ]          while paused: skip to the next beat\n'
  printf '  .          while paused: one step forward\n'
  printf '  Ctrl-C     end the clip\n\n'
  if [ "$n" -lt "$total" ]; then
    printf '  \033[2mAfter this clip: make play-%s\033[0m\n\n' "$((n + 1))"
  fi
  printf '  \033[1mEnter to start\033[0m'
  read -r -s _ </dev/tty
  # The recordings cap waits at 2 s (a policy becoming Ready, the Finale's background scan); 1 s
  # keeps a still screen short and still leaves the 0.5 s beat after each Space.
  asciinema play --quiet --pause-on-markers --idle-time-limit 1 "$cast" || true
done
