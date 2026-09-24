#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root_dir=$(CDPATH= cd -- "$script_dir/../../.." && pwd)
windows_user=${NAVISOMA_WSLC_WIN_USER:-asopitech}
stage_dir="/mnt/c/Users/$windows_user/AppData/Local/Temp/navisoma-wslc-integration"

if [ -e "$stage_dir" ]; then
  echo "refusing to reuse existing stage directory: $stage_dir" >&2
  exit 1
fi

cleanup() {
  rm -rf "$stage_dir"
}
trap cleanup EXIT

"$root_dir/native/wslc/test-build.sh"
mkdir -p "$stage_dir"
cp "$root_dir/native/wslc/.build/navisoma.exe" "$root_dir/native/wslc/.build/wslcsdk.dll" "$stage_dir/"
cp "$script_dir/healthy.compose.yaml" "$script_dir/unhealthy.compose.yaml" "$stage_dir/"
windows_stage=$(wslpath -w "$stage_dir")
"$stage_dir/navisoma.exe" up --backend wslc "$windows_stage\\healthy.compose.yaml"
"$stage_dir/navisoma.exe" down --backend wslc "$windows_stage\\healthy.compose.yaml"
if "$stage_dir/navisoma.exe" up --backend wslc "$windows_stage\\unhealthy.compose.yaml"; then
  echo "unhealthy dependency unexpectedly succeeded" >&2
  exit 1
fi
