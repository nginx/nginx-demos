#!/usr/bin/env bash
# Shared helpers for the demo clips.
#
# Every clip script is written to be recorded: commands are "typed" on screen, then run, and the
# script pauses between beats so you can speak over it live. Set DEMO_AUTO=1 (make check) to
# run without pauses or typing and fail fast if any outcome is not what the clip claims. Set
# DEMO_RECORD=1 (make record) to type as live but, instead of waiting at each pause, print an
# invisible tag that tools/record.sh turns into an asciinema marker (where playback pauses).
#
#   tenant <kubectl args>    run kubectl as team-assistant-dev: tenant-editor in team-assistant only
#   research <kubectl args>  run kubectl as team-research-dev: tenant-editor in team-research only
#                            (the tenant role: env/manifests/01-rbac.yaml)
#   platform <kubectl args>  run kubectl as platform-admin (group platform-admins)
#   show <cmd...>            type and run a command, any exit code is fine
#   expect_ok <cmd...>       type and run, must succeed
#   expect_denied <text> <cmd...>   type and run, must fail with <text> in its output
#                            (use `kubectl create --dry-run=server` to retry an existing object:
#                            `apply` of an unchanged object never reaches admission)
#   say <text>               on-screen caption (what happens next)
#   pause                    wait for Enter (live) / marker (recording) / no-op (make check)
set -uo pipefail

DEMO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=../versions.env
source "$DEMO_ROOT/versions.env"
export PATH="$DEMO_ROOT/tools:$PATH"

DEMO_AUTO=${DEMO_AUTO:-0}
DEMO_RECORD=${DEMO_RECORD:-0}
TYPE_DELAY=${TYPE_DELAY:-0.02}
# An OSC sequence terminals ignore; tools/record.sh turns each one into a marker.
DEMO_MARKER=$'\033]1337;demo-marker\007'

export BOLD=$'\033[1m' DIM=$'\033[2m' RED=$'\033[31m' GREEN=$'\033[32m' YELLOW=$'\033[33m'
export BLUE=$'\033[34m' MAGENTA=$'\033[35m' RESET=$'\033[0m'

tenant()   { kubectl --as=team-assistant-dev "$@"; }
research() { kubectl --as=team-research-dev "$@"; }
platform() { kubectl --as=platform-admin --as-group=platform-admins --as-group=system:authenticated "$@"; }
export -f tenant research platform

fail() { printf '%s\n' "${RED}${BOLD}CHECK FAILED:${RESET} $*" >&2; exit 1; }

clip() {
  clear 2>/dev/null || true
  printf '%s\n\n' "${BOLD}${BLUE}── $* ──${RESET}"
}

# Long lines break at a space, not mid-word, on the terminal the clip runs in (live, or recorded:
# recordings are 100 columns wide so the text can be large on a projector). Off a terminal (make
# check) nothing wraps. tools/wrap.py does it: it keeps indents and ignores colour codes.
DEMO_COLS=0
if [ -t 1 ]; then DEMO_COLS=$(tput cols); fi
wrap() {
  if [ "$DEMO_COLS" -le 0 ]; then cat; return; fi
  python3 "$DEMO_ROOT/tools/wrap.py" "$DEMO_COLS" "$@"
}

# Runs a shown command with its output wrapped. Through a pipe yq would drop its colours, so force
# them (the function is called by the eval'd command).
run_wrapped() {
  if [ "$DEMO_COLS" -le 0 ]; then eval "$*"; return; fi
  # shellcheck disable=SC2329
  (yq() { command yq -C "$@"; }; eval "$*") | wrap
}

say() { printf '\n%s\n' "${MAGENTA}${BOLD}$(printf '# %s' "$*" | wrap)${RESET}"; }

pause() {
  if [ "$DEMO_AUTO" = 1 ]; then return; fi
  if [ "$DEMO_RECORD" = 1 ]; then
    printf '%s' "$DEMO_MARKER"
    sleep 0.5   # after Space on playback, a moment before the next caption starts typing
    return
  fi
  read -r -s -p "" _ </dev/tty
}

type_cmd() {
  local text="$*"
  printf '%s' "${GREEN}\$${RESET} "
  if [ "$DEMO_AUTO" = 1 ]; then printf '%s\n' "$text"; return; fi
  # Typed as it will look: a long command breaks at a space, its next line indented.
  text=$(printf '%s' "$text" | wrap --command)
  local i
  for ((i = 0; i < ${#text}; i++)); do
    printf '%s' "${text:i:1}"
    sleep "$TYPE_DELAY"
  done
  printf '\n'
}

show() {
  type_cmd "$*"
  run_wrapped "$*"
  return 0
}

expect_ok() {
  type_cmd "$*"
  if ! run_wrapped "$*"; then fail "expected success: $*"; fi
}

expect_denied() {
  local want=$1; shift
  type_cmd "$*"
  local out
  if out=$(eval "$*" 2>&1); then
    printf '%s\n' "$out"
    fail "expected denial containing '$want': $*"
  fi
  # Trim kubectl's long prefixes so "Denied by <policy>: <message>" is what's on screen.
  # Handles Kyverno webhook denials and native ValidatingAdmissionPolicy denials.
  # "Tries again" clips use `kubectl create --dry-run=server` on objects that already exist: the
  # API server runs admission before the existence check, so the policy's verdict is what comes
  # back. AlreadyExists lines for unrelated objects in the same file are hidden.
  printf '%s\n' "$out" | sed -E \
    -e '/already exists$/d' \
    -e 's/^Error from server( \([A-Za-z]+\))?: //' \
    -e 's/^error when (creating|applying patch|patching|replacing)[^:]*: //' \
    -e 's/^admission webhook "[^"]*" denied the request: Policy ([a-z0-9-]+) failed: /Denied by \1: /' \
    -e "s/^.*ValidatingAdmissionPolicy '([a-z0-9-]+)' with binding '[^']*' denied request: /Denied by \\1 (native VAP): /" \
    | wrap | sed -E "s/.*/${RED}${BOLD}&${RESET}/"
  grep -q -- "$want" <<<"$out" || fail "denied, but message lacks '$want'"
}

# Apply a demo policy (and the RBAC it needs) as platform-admin, then wait until Kyverno
# reports it Ready so the next command is actually evaluated against it.
install_policy() {
  local dir=$1 name kind
  name=$(yq '.metadata.name' "$DEMO_ROOT/policies/$dir/policy.yaml")
  kind=$(yq '.kind' "$DEMO_ROOT/policies/$dir/policy.yaml" | tr '[:upper:]' '[:lower:]')
  show "platform apply -f policies/$dir/policy.yaml"
  local i
  for ((i = 0; i < 60; i++)); do
    [ "$(kubectl get "$kind" "$name" -o jsonpath='{.status.conditionStatus.ready}' 2>/dev/null)" = true ] && break
    sleep 1
  done
  [ "$i" -lt 60 ] || fail "policy $name not Ready after 60s"
  # Webhook config changes take a moment to reach the API server.
  sleep 3
}

# Wait for a condition (shell expression) without printing anything.
wait_for() {
  local timeout=$1; shift
  local i
  for ((i = 0; i < timeout; i++)); do
    if eval "$*" >/dev/null 2>&1; then return 0; fi
    sleep 1
  done
  fail "timed out after ${timeout}s waiting for: $*"
}

cd "$DEMO_ROOT" || exit 1
