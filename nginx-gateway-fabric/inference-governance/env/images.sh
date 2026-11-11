#!/usr/bin/env bash
# Pulls every image in env/images.txt into the lab's Docker daemon and loads it into the kind
# cluster's nodes.
#
# Why: behind a TLS-intercepting proxy the lab trusts the proxy's CA (CA_BUNDLE, see tools/toolbox)
# but the kind nodes' containerd doesn't, so in-cluster pulls fail with x509 errors.
# Pulling in the lab and side-loading avoids the problem; every manifest uses imagePullPolicy: Never.
#
#   env/images.sh pull    pull only (do this while on a network that works)
#   env/images.sh load    load already-pulled images into the cluster (no network needed)
#   env/images.sh         both
set -euo pipefail
cd "$(dirname "$0")/.."
source versions.env

mode=${1:-all}
mapfile -t IMAGES < <(grep -vE '^\s*(#|$)' env/images.txt)

# Pull for the node architecture, not whatever Docker defaults to (matters on Apple silicon with
# Rosetta, or amd64 hosts with QEMU configured).
case "$(docker info --format '{{.Architecture}}')" in
  aarch64|arm64) PLATFORM=linux/arm64 ;;
  *) PLATFORM=linux/amd64 ;;
esac

check_versions() {
  local want
  for want in "nginx-gateway-fabric:$NGF_VERSION" "llm-d-router-endpoint-picker:$LLMD_ROUTER_VERSION" \
              "llm-d-inference-sim:$INFERENCE_SIM_VERSION" "$CURL_IMAGE"; do
    printf '%s\n' "${IMAGES[@]}" | grep -q -- "$want" \
      || { echo "env/images.txt is out of sync with versions.env (missing $want)" >&2; exit 1; }
  done
}

pull() {
  local img
  for img in "${IMAGES[@]}"; do
    if docker image inspect "$img" >/dev/null 2>&1; then
      printf '  have  %s\n' "$img"
    else
      printf '  pull  %s\n' "$img"
      docker pull -q --platform "$PLATFORM" "$img" >/dev/null
    fi
  done
}

load() {
  kind get clusters 2>/dev/null | grep -qx "$CLUSTER_NAME" \
    || { echo "kind cluster $CLUSTER_NAME doesn't exist yet" >&2; exit 1; }
  local img missing=0
  for img in "${IMAGES[@]}"; do
    docker image inspect "$img" >/dev/null 2>&1 || { echo "  missing on host: $img" >&2; missing=1; }
  done
  [ "$missing" = 0 ] || { echo "run: env/images.sh pull" >&2; exit 1; }
  echo "  loading ${#IMAGES[@]} images into kind/$CLUSTER_NAME (one archive, all nodes)"
  # One `docker save` for all images is much faster than one `kind load` per image.
  local dir archive
  dir=$(mktemp -d "${TMPDIR:-/tmp}/igd-images.XXXXXX")   # portable across GNU and BSD/macOS mktemp
  archive="$dir/images.tar"
  # With Docker Desktop's containerd image store, `docker save` exports each image's full
  # multi-platform index while only $PLATFORM was pulled, and kind's import fails on the missing
  # content ("content digest ... not found"). Save just the node's platform when docker supports it.
  local save_args=()
  docker save --help 2>/dev/null | grep -q -- '--platform' && save_args=(--platform "$PLATFORM")
  docker save "${save_args[@]}" -o "$archive" "${IMAGES[@]}"
  kind load image-archive "$archive" --name "$CLUSTER_NAME"
  rm -rf "$dir"
}

check_versions
case $mode in
  pull) pull ;;
  load) load ;;
  all) pull; load ;;
  *) echo "usage: $0 [pull|load]" >&2; exit 2 ;;
esac
