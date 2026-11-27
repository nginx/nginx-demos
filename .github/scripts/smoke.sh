#!/usr/bin/env bash
# Runs a demo's smoke test, <demo-dir>/tests/ci.sh, from the demo's directory:
#
#   .github/scripts/smoke.sh nginx/api-steering
#
# CI runs this for every demo a pull request changes (.github/workflows/demos.yml).
set -euo pipefail
cd "$(dirname "$0")/../.."
demo=${1:?usage: $0 <demo-dir>}
test -x "$demo/tests/ci.sh" || { echo "$demo has no executable tests/ci.sh" >&2; exit 2; }
cd "$demo"
exec tests/ci.sh
