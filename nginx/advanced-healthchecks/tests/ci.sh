#!/usr/bin/env bash
# Smoke test: the demo runs its two Python apps on the host (see their READMEs), so this installs
# each app's pinned requirements.txt with --require-hashes in the Python image its lockfile was
# compiled for, starts the app, and checks it answers. Run from the repo root with
# .github/scripts/smoke.sh nginx/advanced-healthchecks.
set -euo pipefail
# shellcheck source=../../../.github/scripts/smoke-lib.sh
source "$(git rev-parse --show-toplevel)/.github/scripts/smoke-lib.sh"

python=$(sed -n 's/^FROM \(python:[^ ]*\)$/\1/p' ../api-steering/backend/Dockerfile)

run_app() { # <name> <dir> <script> <port>
  echo "==> start healthchecks-$1"
  docker run -d --name "smoke-healthchecks-$1" -p "127.0.0.1:$4:$4" -v "$PWD/$2:/app:ro" -w /app "$python" \
    sh -c "pip install -q --no-cache-dir --root-user-action=ignore --require-hashes -r requirements.txt && exec python $3" >/dev/null
  SMOKE_CONTAINERS+=("smoke-healthchecks-$1")
}
run_app health-api health-api serverstats.py 5000
run_app webserver webserver webserver.py 8080

wait_http http://127.0.0.1:5000/stats
expect "health API: stats" 200 '"virtualMemory"' http://127.0.0.1:5000/stats
wait_http http://127.0.0.1:8080/
expect "webserver" 200 - http://127.0.0.1:8080/
