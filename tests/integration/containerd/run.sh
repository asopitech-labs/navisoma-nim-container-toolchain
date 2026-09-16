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
  # `timeout` here is a hard ceiling on this whole suite ever hanging in CI — never a substitute
  # for navisoma's own healthcheck.timeout enforcement, which the "timeout fixture" case below
  # actually exercises (and expects to finish in well under this ceiling). 180s leaves headroom
  # for a slow/cold image pull or a loaded build host, neither of which navisoma itself controls.
  timeout 180 "$CONTAINER_ENGINE" run --rm \
    -v "$ROOT:/workspace:Z" \
    -v "$CD_DIR/.dev-run:/run/containerd:Z" \
    navisoma-containerd-build "$BIN" "$@" 2>&1
}

ctr_ns() {
  "$CONTAINER_ENGINE" exec nvsm-containerd-dev ctr -n navisoma "$@"
}

# Container/task ids are namespaced per compose-file invocation (nvsm-<projectId>-<service> —
# see cli.nim's projectIdFor and containerd_backend.nim's newContainerdPort), so assertions here
# match on the "-<service> RUNNING" suffix instead of an exact bare-name id.
check_running() {
  local desc="$1"; shift
  local running
  running=$(ctr_ns tasks list | awk 'NR>1 {print $1, $3}')
  check "$desc: task count" "$(echo "$running" | grep -c .)" "$#"
  for svc in "$@"; do
    check "$desc: $svc running" "$(echo "$running" | grep -c -- "-${svc} RUNNING\$")" "1"
  done
}

"$CD_DIR/dev-daemon.sh" up
cleanup() { "$CD_DIR/dev-daemon.sh" down; }
trap cleanup EXIT

"$CONTAINER_ENGINE" build -t navisoma-containerd-build -f "$CD_DIR/Dockerfile" "$CD_DIR" >/dev/null
"$CONTAINER_ENGINE" run --rm \
  -v "$ROOT:/workspace:Z" \
  navisoma-containerd-build bash -c \
  "cd /workspace && nim c --path:src -d:navisomaContainerd -o:native/containerd/.dev-run/navisoma-cd src/navisoma.nim"

# Pulls and unpacks the one image every fixture uses into the native snapshotter ahead of time, with its own
# generous ceiling separate from run_navisoma's 180s — a slow/cold registry pull is real-world
# network variance that has nothing to do with any fixture's own correctness, and must never be
# what makes the *first* fixture's timing-sensitive assertions flaky. Every navisoma resolve_image
# call after this finds both content and the native snapshot already present.
echo "=== warming image cache (network-bound; not part of any fixture's own timing) ==="
timeout 280 "$CONTAINER_ENGINE" exec nvsm-containerd-dev ctr -n navisoma images pull --snapshotter native docker.io/library/busybox:latest >/dev/null

echo "=== healthy fixture: up ==="
set +e
run_navisoma up --backend containerd "tests/integration/containerd/healthy.compose.yaml"
upExit=$?
set -e
check "healthy up exit code" "$upExit" "0"
check_running "healthy up" api db

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

echo "=== env fixture: healthcheck sees the service's own environment ==="
set +e
run_navisoma up --backend containerd "tests/integration/containerd/env.compose.yaml"
upExit=$?
set -e
check "env up exit code" "$upExit" "0"
check_running "env up" api db
run_navisoma down --backend containerd "tests/integration/containerd/env.compose.yaml" >/dev/null
check "env down: no containers left" "$(ctr_ns containers list | wc -l)" "1"

echo "=== env-override fixture: Compose environment replaces the image's own env, not alongside it ==="
set +e
run_navisoma up --backend containerd "tests/integration/containerd/env-override.compose.yaml"
upExit=$?
set -e
check "env-override up exit code" "$upExit" "0"
run_navisoma down --backend containerd "tests/integration/containerd/env-override.compose.yaml" >/dev/null
check "env-override down: no containers left" "$(ctr_ns containers list | wc -l)" "1"

echo "=== timeout fixture: a probe that outlives healthcheck.timeout fails instead of hanging ==="
set +e
start=$(date +%s)
upOutput=$(run_navisoma up --backend containerd "tests/integration/containerd/timeout.compose.yaml")
upExit=$?
elapsed=$(( $(date +%s) - start ))
set -e
check "timeout up exit code" "$upExit" "1"
check "timeout up: db never started api" "$(echo "$upOutput" | grep -c 'unhealthy')" "1"
if [[ "$elapsed" -gt 90 ]]; then
  echo "FAIL timeout up: took ${elapsed}s — exec_health_probe did not enforce its deadline" >&2
  failures=$((failures + 1))
else
  echo "OK   timeout up: finished in ${elapsed}s (bounded by healthcheck.timeout, not the probe's own runtime of ~999999s)"
fi
check "timeout up: nothing left running (reverse cleanup)" "$(ctr_ns containers list | wc -l)" "1"

echo "=== collision fixtures: two different projects using the same service name never collide ==="
run_navisoma up --backend containerd "tests/integration/containerd/collision-a/compose.yaml" >/dev/null
run_navisoma up --backend containerd "tests/integration/containerd/collision-b/compose.yaml" >/dev/null
check "collision: both projects' containers coexist" "$(ctr_ns containers list | wc -l)" "3"
run_navisoma down --backend containerd "tests/integration/containerd/collision-a/compose.yaml" >/dev/null
check "collision: down(a) leaves b's container running" "$(ctr_ns containers list | wc -l)" "2"
run_navisoma down --backend containerd "tests/integration/containerd/collision-b/compose.yaml" >/dev/null
check "collision: down(b) leaves nothing running" "$(ctr_ns containers list | wc -l)" "1"

echo ""
if [[ "$failures" -eq 0 ]]; then
  echo "ALL OK"
else
  echo "$failures FAILURE(S)"
  exit 1
fi
