#!/bin/sh
set -eu

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary="${TMPDIR:-/tmp}/navisoma-cli-plan-$$"
fixture="$root_dir/tests/integration/containerd/healthy.compose.yaml"
trap 'rm -f "$binary"' EXIT HUP INT TERM

nim c --path:"$root_dir/src" -o:"$binary" "$root_dir/src/navisoma.nim" >/dev/null
containerd_trace=$("$binary" plan --backend containerd "$fixture")
wslc_trace=$("$binary" plan --backend wslc "$fixture")

[ -n "$containerd_trace" ]
[ "$containerd_trace" = "$wslc_trace" ]
