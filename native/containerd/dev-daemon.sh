#!/usr/bin/env bash
# Starts/stops the disposable containerd daemon (Dockerfile.daemon) used for #18 Phase 3
# real-host verification. Never a persistent fixture: `up` always removes any previous instance
# first, and the container is `--rm` so `down`/a crash leaves nothing behind.
#
#   dev-daemon.sh up      build the image (if needed) and start a fresh daemon
#   dev-daemon.sh down    stop and remove it
#   dev-daemon.sh sock    print the containerd.sock path — the same path inside the daemon
#                         container, on the host, and inside any other container that mounts
#                         ./.dev-run at /run/containerd (see test-shim.sh)
#
# Container notes (all discovered by hand while validating this image — see commit history):
#   - Must run --privileged: containerd/runc need real cgroup/mount/namespace capabilities.
#   - Use the "native" snapshotter, not containerd's own default "overlayfs" (pass
#     `--snapshotter native` to `ctr`; the shim's own Snapshots.Prepare calls do the same) —
#     nested overlayfs-on-overlayfs mounts fail under rootless podman's own overlay storage.
#   - entrypoint.sh relocates containerd into a child cgroup and delegates controllers before
#     exec'ing it, working around rootless-podman's private cgroup v2 namespace otherwise
#     leaving containerd itself as a "member process" of the namespace root, which blocks runc
#     from creating/delegating child cgroups for tasks (cgroup v2's no-internal-process rule).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

CONTAINER_ENGINE="${CONTAINER_ENGINE:-podman}"
NAME="nvsm-containerd-dev"
IMAGE="navisoma-containerd-daemon"

case "${1:-}" in
  up)
    "$CONTAINER_ENGINE" rm -f "$NAME" >/dev/null 2>&1 || true
    "$CONTAINER_ENGINE" build -t "$IMAGE" -f Dockerfile.daemon .
    rm -rf .dev-run && mkdir -p .dev-run
    "$CONTAINER_ENGINE" run --rm --privileged --name "$NAME" \
      -v "$(pwd)/.dev-run:/run/containerd:Z" \
      -d "$IMAGE" >/dev/null
    echo "waiting for containerd to be ready..."
    for _ in $(seq 1 30); do
      if "$CONTAINER_ENGINE" exec "$NAME" ctr version >/dev/null 2>&1; then
        echo "containerd is up (container: $NAME)"
        exit 0
      fi
      sleep 0.5
    done
    echo "containerd did not become ready in time" >&2
    "$CONTAINER_ENGINE" logs "$NAME" >&2
    exit 1
    ;;
  down)
    "$CONTAINER_ENGINE" rm -f "$NAME" >/dev/null 2>&1 || true
    rm -rf .dev-run 2>/dev/null || true
    ;;
  sock)
    echo "/run/containerd/containerd.sock"
    ;;
  *)
    echo "usage: $0 {up|down|sock}" >&2
    exit 1
    ;;
esac
