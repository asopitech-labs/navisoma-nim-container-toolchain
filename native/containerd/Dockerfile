# Build/codegen container for the #18 Phase 3 containerd lowering.
#
# Nothing this Dockerfile installs (protoc, grpc_cpp_plugin, the grpc/protobuf C++ dev
# headers/libs, Nim) is installed on the host — per project direction, all native-toolchain
# work for backend lowering happens inside a container. This image is used for two things:
#
#   1. Regenerating native/containerd/generated/ from native/containerd/proto/ (codegen —
#      only needed when the pinned containerd proto version bumps; output is committed).
#   2. Compiling/linking the -d:navisomaContainerd build (`nimble build/test -d:navisomaContainerd`)
#      — every `nim c` invocation for that build must run inside this image, since only it has
#      the grpc/protobuf C++ headers/libs the {.passC.}/{.passL.} pragmas in
#      src/native/containerd_raw.nim point at.
#
# protoc and libprotobuf-dev are pinned to the SAME Ubuntu package version deliberately —
# protobuf C++ does not promise ABI stability across releases, so generated code must be
# compiled/linked against the exact runtime it was generated against
# (docs/c-cpp-integration-assets.md "Protobuf version pinning").

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
        protobuf-compiler=3.21.12-8.2ubuntu0.3 \
        protobuf-compiler-grpc=1.51.1-4.1build5 \
        libprotobuf-dev=3.21.12-8.2ubuntu0.3 \
        libgrpc++-dev=1.51.1-4.1build5 \
        nlohmann-json3-dev=3.11.3-1 \
        g++ \
        pkg-config \
        ca-certificates \
        curl xz-utils git \
    && rm -rf /var/lib/apt/lists/*

# Nim, pinned to the same version the host dev environment uses (nimble.requires >= 2.0.0).
ARG NIM_VERSION=2.2.10
RUN curl -fsSL "https://nim-lang.org/download/nim-${NIM_VERSION}-linux_x64.tar.xz" -o /tmp/nim.tar.xz \
    && tar -C /opt -xJf /tmp/nim.tar.xz \
    && mv "/opt/nim-${NIM_VERSION}" /opt/nim \
    && rm /tmp/nim.tar.xz
ENV PATH="/opt/nim/bin:${PATH}"

# Pinned to the same version navisoma.nimble's `requires "yaml >= 2.2.1"` resolves to on the
# host, so a -d:navisomaContainerd build here uses identical dependency versions to `nimble test`.
RUN nimble install -y yaml@2.2.1

WORKDIR /workspace
