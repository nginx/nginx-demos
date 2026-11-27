#!/usr/bin/env bash
# Helpers for the demos' smoke tests (<demo>/tests/ci.sh). Each test builds the demo's images,
# starts them, and checks they answer. Run one with:
#
#   .github/scripts/smoke.sh nginx/api-steering
#
# Images are tagged smoke-<name> and containers are named smoke-<name>; both are removed on exit.
set -euo pipefail

SMOKE_CONTAINERS=()
SMOKE_IMAGES=()
SMOKE_FAILED=0

smoke_cleanup() {
  local c
  for c in ${SMOKE_CONTAINERS[@]+"${SMOKE_CONTAINERS[@]}"}; do
    if [ "$SMOKE_FAILED" != 0 ]; then
      echo "--- logs: $c"
      docker logs "$c" 2>&1 | tail -40 || true
    fi
    docker rm -f "$c" >/dev/null 2>&1 || true
  done
  [ ${#SMOKE_IMAGES[@]} -eq 0 ] || docker rmi -f "${SMOKE_IMAGES[@]}" >/dev/null 2>&1 || true
}
trap 'SMOKE_FAILED=${SMOKE_FAILED:-$?}; smoke_cleanup' EXIT
trap 'SMOKE_FAILED=1' ERR

# build <name> <context> [docker build args...]
build() {
  local name=$1 context=$2
  shift 2
  echo "==> build $name"
  docker build -q -t "smoke-$name" "$@" "$context" >/dev/null
  SMOKE_IMAGES+=("smoke-$name")
}

# start <name> <host-port>:<container-port> [docker run args...] [-- command...]
# Runs image smoke-<name> detached, with the port published on 127.0.0.1.
start() {
  local name=$1 ports=$2
  shift 2
  echo "==> start $name"
  docker run -d --name "smoke-$name" -p "127.0.0.1:$ports" "$@" >/dev/null
  SMOKE_CONTAINERS+=("smoke-$name")
}

# wait_http <url> [curl args...]: wait up to 60 seconds for any HTTP response.
wait_http() {
  local url=$1
  shift
  for _ in $(seq 1 60); do
    if curl -s -o /dev/null "$@" "$url"; then return 0; fi
    sleep 1
  done
  echo "FAIL: nothing answered on $url" >&2
  SMOKE_FAILED=1
  return 1
}

# expect <label> <status> <body-substring|-> <url> [curl args...]
expect() {
  local label=$1 want=$2 needle=$3 url=$4 body code
  shift 4
  body=$(curl -s -w '\n%{http_code}' "$@" "$url") || true
  code=${body##*$'\n'}
  body=${body%$'\n'*}
  if [ "$code" != "$want" ]; then
    echo "FAIL: $label: expected HTTP $want, got $code: ${body:0:300}" >&2
    SMOKE_FAILED=1
    return 1
  fi
  if [ "$needle" != - ] && ! grep -qF -- "$needle" <<<"$body"; then
    echo "FAIL: $label: response doesn't contain '$needle': ${body:0:300}" >&2
    SMOKE_FAILED=1
    return 1
  fi
  echo "ok: $label"
}
