#!/usr/bin/env bash
# Regenerates native/containerd/generated/ from native/containerd/proto/ using the pinned
# build container (native/containerd/Dockerfile). Only run this to bump the pinned containerd
# proto version — normal builds consume the committed output in generated/ directly.
#
# protoc/grpc_cpp_plugin/cmake are never installed on the host for this project; this script
# always runs them inside the container. Requires podman (or docker).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

CONTAINER_ENGINE="${CONTAINER_ENGINE:-podman}"

"$CONTAINER_ENGINE" build -t navisoma-containerd-build -f Dockerfile .

"$CONTAINER_ENGINE" run --rm -v "$(pwd):/workspace:Z" navisoma-containerd-build bash -c '
  set -euo pipefail
  rm -rf generated
  mkdir -p generated
  protoc \
    --proto_path=proto \
    --cpp_out=generated \
    --grpc_out=generated \
    --plugin=protoc-gen-grpc=/usr/bin/grpc_cpp_plugin \
    proto/services/transfer/v1/transfer.proto \
    proto/services/containers/v1/containers.proto \
    proto/services/images/v1/images.proto \
    proto/services/tasks/v1/tasks.proto \
    proto/services/snapshots/v1/snapshots.proto \
    proto/services/content/v1/content.proto \
    proto/types/transfer/registry.proto \
    proto/types/transfer/imagestore.proto \
    proto/types/descriptor.proto \
    proto/types/mount.proto \
    proto/types/metrics.proto \
    proto/types/platform.proto \
    proto/types/task/task.proto
'

echo "Regenerated native/containerd/generated/. Review the diff, update generated/MANIFEST.md" \
     "and proto/MANIFEST.md together, and commit both in one change."
