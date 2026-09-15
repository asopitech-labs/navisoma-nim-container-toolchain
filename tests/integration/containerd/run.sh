#!/usr/bin/env bash
# The "disposable real-host scenario" issue #18 phase 3 calls for: runs the fixed MVP fixture
# through `navisoma up`/`down --backend containerd` against a fresh containerd daemon, for both
# the healthy path and the unhealthy-dependency path, asserting the same journal/cleanup
# guarantees tests/test_executor.nim already proves against the fake backend.
#
# Not run by `nimble test` (no container/daemon dependency there) — run this explicitly:
#   tests/integration/containerd/run.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
CD_DIR="$ROOT/native/containerd"
CONTAINER_ENGINE="${CONTAINER_ENGINE:-podman}"
FIXTURES="$ROOT/tests/integration/containerd"
BIN="/workspace/native/containerd/.dev-run/navisoma-cd"  # in-container path — see run_navisoma()

failures=0
check() {
  local desc="$1" got="$2" want="$3"
  if [[ "$got" == "$want" ]]; then
    echo "OK   $desc"
  else
    echo "FAIL $desc: got '$got' want '$want'" >&2
    failures=$((failures + 1))
  fi
}

run_navisoma() {
  "$CONTAINER_ENGINE" run --rm \
    -v "$ROOT:/workspace:Z" \
    -v "$CD_DIR/.dev-run:/run/containerd:Z" \
    navisoma-containerd-build "$BIN" "$@" 2>&1
}

ctr_ns() {
  "$CONTAINER_ENGINE" exec nvsm-containerd-dev ctr -n navisoma "$@"
}

"$CD_DIR/dev-daemon.sh" up
cleanup() { "$CD_DIR/dev-daemon.sh" down; }
trap cleanup EXIT

"$CONTAINER_ENGINE" build -t navisoma-containerd-build -f "$CD_DIR/Dockerfile" "$CD_DIR" >/dev/null
"$CONTAINER_ENGINE" run --rm \
  -v "$ROOT:/workspace:Z" \
  navisoma-containerd-build bash -c \
  "cd /workspace && nim c --path:src -d:navisomaContainerd -o:native/containerd/.dev-run/navisoma-cd src/navisoma.nim"

echo "=== healthy fixture: up ==="
set +e
run_navisoma up --backend containerd "tests/integration/containerd/healthy.compose.yaml"
upExit=$?
set -e
check "healthy up exit code" "$upExit" "0"

running=$(ctr_ns tasks list | awk 'NR>1 {print $1, $3}' | sort)
check "healthy up: db and api both running" "$running" "$(printf 'api RUNNING\ndb RUNNING')"

echo "=== healthy fixture: down ==="
set +e
run_navisoma down --backend containerd "tests/integration/containerd/healthy.compose.yaml"
downExit=$?
set -e
check "healthy down exit code" "$downExit" "0"
check "healthy down: no containers left" "$(ctr_ns containers list | wc -l)" "1"
check "healthy down: no tasks left" "$(ctr_ns tasks list | wc -l)" "1"

echo "=== unhealthy fixture: up ==="
set +e
upOutput=$(run_navisoma up --backend containerd "tests/integration/containerd/unhealthy.compose.yaml")
upExit=$?
set -e
check "unhealthy up exit code" "$upExit" "1"
check "unhealthy up: db never started api" "$(echo "$upOutput" | grep -c 'unhealthy')" "1"
check "unhealthy up: nothing left running (reverse cleanup)" "$(ctr_ns containers list | wc -l)" "1"

echo ""
if [[ "$failures" -eq 0 ]]; then
  echo "ALL OK"
else
  echo "$failures FAILURE(S)"
  exit 1
fi
