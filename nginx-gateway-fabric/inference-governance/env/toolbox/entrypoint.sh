#!/bin/sh
# Lab entrypoint: trust the CA bundle tools/toolbox mounted (if any), then hand over to the
# stock dind entrypoint, which starts dockerd. Runs before dockerd, so image pulls trust it too.
set -eu
if [ -s /etc/ca-bundle.pem ]; then
  # update-ca-certificates wants one certificate per file.
  awk '/BEGIN CERTIFICATE/ {n++} {f = "/usr/local/share/ca-certificates/ca-bundle-" n ".crt"; print > f}' /etc/ca-bundle.pem
  update-ca-certificates 2>/dev/null
fi
exec dockerd-entrypoint.sh "$@"
