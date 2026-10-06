#!/bin/sh
set -eu

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary="${TMPDIR:-/tmp}/navisoma-cli-plan-$$"
fixture="$root_dir/tests/integration/containerd/healthy.compose.yaml"
migration_fixture="$root_dir/tests/integration/containerd/migration.compose.yaml"
trap 'rm -f "$binary"' EXIT HUP INT TERM

nim c --path:"$root_dir/src" -o:"$binary" "$root_dir/src/navisoma.nim" >/dev/null
containerd_trace=$("$binary" plan --backend containerd "$fixture")
wslc_trace=$("$binary" plan --backend wslc "$fixture")

[ -n "$containerd_trace" ]
[ "$containerd_trace" = "$wslc_trace" ]

migration_containerd_trace=$("$binary" plan --backend containerd "$migration_fixture")
migration_wslc_trace=$("$binary" plan --backend wslc "$migration_fixture")
expected_migration_trace='ResolveImage(db)
CreateContainer(db)
StartContainer(db)
AwaitHealth(db)
ResolveImage(migrate)
CreateContainer(migrate)
StartContainer(migrate)
AwaitCompletion(migrate)
ResolveImage(api)
CreateContainer(api)
StartContainer(api)'
[ "$migration_containerd_trace" = "$expected_migration_trace" ]
[ "$migration_containerd_trace" = "$migration_wslc_trace" ]
