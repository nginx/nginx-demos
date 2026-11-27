#!/usr/bin/env bash
# Smoke test: builds the lab image (lab/Dockerfile, with the build args lab/toolbox passes from
# lab/versions.env) and runs what `make test` and `make lint` run in it: every policy's Kyverno CLI
# tests and shellcheck + yamllint. Neither needs the lab's Docker daemon or a cluster, so they run
# in a throwaway container instead of the long-running lab. `make up` (a kind cluster, 5-8 minutes)
# isn't run here. Run from the repo root with
# .github/scripts/smoke.sh nginx-gateway-fabric/inference-governance-with-kyverno.
set -euo pipefail
# shellcheck source=../../../.github/scripts/smoke-lib.sh
source "$(git rev-parse --show-toplevel)/.github/scripts/smoke-lib.sh"

# shellcheck disable=SC2207  # one word per build arg, as lab/toolbox builds them
build_args=($(sed -nE 's/^([A-Z_]+_(IMAGE|VERSION))=([^ #]+).*/--build-arg \1=\3/p' lab/versions.env))
build inference-governance-lab lab "${build_args[@]}"

lab() { docker run --rm -v "$PWD:/demo:ro" -w /demo --entrypoint bash smoke-inference-governance-lab -c "$1"; }

# Keep these in step with the Makefile's test and lint targets.
echo "==> make test (Kyverno CLI)"
lab 'kyverno test scenarios/ setup/tests/ --require-tests --remove-color'
echo "==> make lint"
lab 'shellcheck -x -P SCRIPTDIR lab/toolbox setup/*.sh setup/*/*.sh scenarios/*.sh scenarios/*/*.sh && yamllint -c .yamllint.yaml .'
echo "==> tools in the lab"
lab 'kubectl version --client && helm version --short && kind version && yq --version && kyverno version | head -3'
echo "ok: lab builds, policy tests and lint pass"
