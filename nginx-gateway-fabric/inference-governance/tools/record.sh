#!/usr/bin/env bash
# Records one clip with asciinema, for playback with `make play` (recorded rather than live).
#
#   tools/record.sh <clip> <out.cast>     e.g. tools/record.sh borrow-a-model/before recordings/01-borrow-a-model-before.cast
#
# The clip runs with DEMO_RECORD=1: it types as it would live, and each pause prints an invisible
# tag instead of waiting for Enter. This turns every tag into an asciinema marker, so playback with
# --pause-on-markers stops exactly where the live clip waits. The clip's own checks still apply: a
# clip that doesn't behave as it says fails the recording.
set -euo pipefail
cd "$(dirname "$0")/.."

clip=$1 out=$2
# Playback needs a terminal at least this big (see make play). 100 columns lets the text be large
# on a projector; the clips wrap long lines to fit.
SIZE=${RECORD_SIZE:-100x28}
raw=$(mktemp)
trap 'rm -f "$raw"' EXIT

DEMO_RECORD=1 TYPE_DELAY=${TYPE_DELAY:-0.03} asciinema rec --headless --quiet --overwrite --return \
  --window-size "$SIZE" --idle-time-limit 2 --title "$clip" \
  --command "scenarios/$clip.sh" "$raw"

python3 - "$raw" "$out" <<'PY'
import json, sys

TAG = "\x1b]1337;demo-marker\x07"
src, dst = sys.argv[1], sys.argv[2]
lines = open(src).read().splitlines()
out, markers, pending, carry = [lines[0]], 0, "", 0.0
for line in lines[1:]:
    t, code, data = json.loads(line)
    if code != "o":
        out.append(line)
        continue
    data, t = pending + data, t + carry
    pending, carry = "", 0.0
    # A tag split across two output events: hold the start until the rest arrives.
    for k in range(len(TAG) - 1, 0, -1):
        if data.endswith(TAG[:k]):
            data, pending = data[:-k], TAG[:k]
            break
    parts = data.split(TAG)
    for i, part in enumerate(parts):
        if i:
            markers += 1
            out.append(json.dumps([0.0, "m", ""]))
        if part:
            out.append(json.dumps([t if i == 0 else 0.0, "o", part]))
            t = 0.0
    carry = t   # time of an event that was only a tag carries to the next one
open(dst, "w").write("\n".join(out) + "\n")
print(f"recorded {dst.split('/recordings/')[-1]}: {markers} pauses")
PY
