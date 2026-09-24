# Build container for the #18 Phase 4 WSLC lowering.
#
# Nothing this Dockerfile installs (Zig, the WSLC SDK header/import library, Nim) is installed
# on the host — per project direction, all native-toolchain work for backend lowering happens
# inside a container. Unlike native/containerd's build (Linux ELF, native gcc/g++), this one
# cross-compiles a genuine Windows PE binary from Linux: WSLC's C API (wslcsdk.dll) is a
# COM/WinRT-backed Windows DLL that only a real Windows process can call — there is no way to
# reach it from a Linux ELF process, containerized or not (see docs/wslc-dev-environment.md).
#
# Zig is the cross-compiler, exactly as docs/validation/wslc-capability-gate/README.md's probe
# used it: `zig cc -target x86_64-windows-gnu` bundles clang+lld and produces a real Windows PE
# binary without a system MinGW or MSVC install. Nim is configured (see
# src/native/wslc_raw.nim) to invoke it as its C/C++ compiler for the -d:navisomaWslc build.

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl unzip xz-utils git \
    && rm -rf /var/lib/apt/lists/*

# Pinned to the same version docs/validation/wslc-capability-gate/README.md's probe used.
ARG ZIG_VERSION=0.16.0
RUN curl -fsSL "https://ziglang.org/download/${ZIG_VERSION}/zig-x86_64-linux-${ZIG_VERSION}.tar.xz" -o /tmp/zig.tar.xz \
    && tar -C /opt -xJf /tmp/zig.tar.xz \
    && mv "/opt/zig-x86_64-linux-${ZIG_VERSION}" /opt/zig \
    && rm /tmp/zig.tar.xz
ENV PATH="/opt/zig:${PATH}"

ARG WSLC_SDK_VERSION=2.9.9
ARG WSLC_SDK_SHA256=7764355a4fa3ca29d3c8a1dc2a0f4ee1c9c4603d110a20218aef5cd2efa90fe2
ARG WSLC_HEADER_SHA256=e2867c46e51db62b7307dc858c2f5cd767d298760b272ad19a161145bd1bbba1
ARG WSLC_X64_LIB_SHA256=c2a1023c8f97253c478b5e97df609898410534111cae3086f3eee9a5942bd526
ARG WSLC_X64_DLL_SHA256=8d4d55d4283fb32a5909b57e78b576d01363d7b28bb9b2595115e80faf61db5b
RUN curl -fsSL "https://api.nuget.org/v3-flatcontainer/microsoft.wsl.containers/${WSLC_SDK_VERSION}/microsoft.wsl.containers.${WSLC_SDK_VERSION}.nupkg" -o /tmp/wslc.nupkg \
    && echo "${WSLC_SDK_SHA256}  /tmp/wslc.nupkg" | sha256sum -c - \
    && mkdir /opt/navisoma-wslc-sdk \
    && unzip -q /tmp/wslc.nupkg -d /opt/navisoma-wslc-sdk \
    && echo "${WSLC_HEADER_SHA256}  /opt/navisoma-wslc-sdk/include/wslcsdk.h" | sha256sum -c - \
    && echo "${WSLC_X64_LIB_SHA256}  /opt/navisoma-wslc-sdk/runtimes/win-x64/wslcsdk.lib" | sha256sum -c - \
    && echo "${WSLC_X64_DLL_SHA256}  /opt/navisoma-wslc-sdk/runtimes/win-x64/native/wslcsdk.dll" | sha256sum -c - \
    && rm /tmp/wslc.nupkg

# Nim, pinned to the same version the host dev environment uses (nimble.requires >= 2.0.0) —
# identical to native/containerd/Dockerfile's pin, for the same reason.
ARG NIM_VERSION=2.2.10
RUN curl -fsSL "https://nim-lang.org/download/nim-${NIM_VERSION}-linux_x64.tar.xz" -o /tmp/nim.tar.xz \
    && tar -C /opt -xJf /tmp/nim.tar.xz \
    && mv "/opt/nim-${NIM_VERSION}" /opt/nim \
    && rm /tmp/nim.tar.xz
ENV PATH="/opt/nim/bin:${PATH}"

RUN nimble install -y yaml@2.2.1

WORKDIR /workspace
