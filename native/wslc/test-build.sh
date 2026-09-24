#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
engine=${CONTAINER_ENGINE:-podman}
image=navisoma-wslc-build
output_dir="$script_dir/.build"

mkdir -p "$output_dir"
"$engine" build -t "$image" -f "$script_dir/Dockerfile" "$root_dir"
"$engine" run --rm -v "$root_dir:/workspace:Z" -w /workspace "$image" \
  nim c --path:src --os:windows --cpu:amd64 --cc:clang \
    --clang.exe:/workspace/native/wslc/zig-cc.sh \
    --clang.linkerexe:/workspace/native/wslc/zig-cc.sh \
    -d:navisomaWslc -o:/workspace/native/wslc/.build/navisoma.exe src/navisoma.nim
"$engine" run --rm -v "$root_dir:/workspace:Z" -w /workspace "$image" \
  cp /opt/navisoma-wslc-sdk/runtimes/win-x64/native/wslcsdk.dll /workspace/native/wslc/.build/
