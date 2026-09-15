#!/usr/bin/env bash
# Native-layer smoke test: compiles containerd_bridge.{h,cc} + shim/shim_smoke_test.cc inside
# the build container and runs it against a disposable containerd daemon (dev-daemon.sh).
# Exercises every BackendPort operation through the shim's actual C ABI — independent of Nim —
# per docs/architecture.md section 15's "native interop tests" requirement.
#
#   test-shim.sh            starts its own disposable daemon, runs the test, tears it down
#   test-shim.sh --keep-daemon   leaves the daemon running afterwards (faster iteration)
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

CONTAINER_ENGINE="${CONTAINER_ENGINE:-podman}"
KEEP_DAEMON=false
[[ "${1:-}" == "--keep-daemon" ]] && KEEP_DAEMON=true

./dev-daemon.sh up

cleanup() {
  if [[ "$KEEP_DAEMON" != "true" ]]; then
    ./dev-daemon.sh down
  fi
}
trap cleanup EXIT

"$CONTAINER_ENGINE" build -t navisoma-containerd-build -f Dockerfile . >/dev/null

"$CONTAINER_ENGINE" run --rm \
  -v "$(pwd):/workspace:Z" \
  -v "$(pwd)/.dev-run:/run/containerd:Z" \
  navisoma-containerd-build bash -c '
    set -e
    cd /workspace
    g++ -std=c++17 -o /workspace/.dev-run/shim_smoke_test \
      shim/containerd_bridge.cc shim/shim_smoke_test.cc \
      $(find generated -name "*.pb.cc") \
      $(pkg-config --cflags --libs protobuf grpc++) \
      -Igenerated -Ishim
    /workspace/.dev-run/shim_smoke_test
  '
