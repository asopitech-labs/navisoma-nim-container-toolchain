#!/usr/bin/env bash
# NAVISOMA / Issue #15 - reproducible stage -> run -> cleanup-verify driver.
#
# Not part of the NAVISOMA product. This is the "outer harness" that owns
# fixture staging, which the probe itself cannot own (it can't copy its own
# .exe into place before it exists). Responsibilities are split cleanly:
#   - the probe (probe.c) owns every WSLC-side resource it creates
#     (sessions, containers, images, volumes) AND the caller-owned session
#     storage directories, deleting and verifying each one itself;
#   - this driver owns the staged input files (probe.exe, wslcsdk.dll, the
#     two fixture tars) and the parent staging directory, deleting and
#     verifying those after the probe exits.
#
# Usage:
#   run-probe.sh <probe.exe> <wslcsdk.dll> <import-fixture.tar> <build-output.tar> [times]
#
# Runs the full stage -> run -> verify-parent-dir-gone cycle `times` times
# (default 2) from what should be a clean state each time, run in
# WINDIR_STAGING under the current Windows user's Temp directory.

set -euo pipefail

if [ "$#" -lt 4 ]; then
    echo "usage: $0 <probe.exe> <wslcsdk.dll> <import-fixture.tar> <build-output.tar> [times]" >&2
    exit 1
fi

PROBE_EXE="$1"
WSLCSDK_DLL="$2"
IMPORT_FIXTURE="$3"
BUILD_OUTPUT="$4"
TIMES="${5:-2}"

WIN_USER="${NAVISOMA_WSLC_WIN_USER:-asopitech}"
STAGING_DIR="/mnt/c/Users/${WIN_USER}/AppData/Local/Temp/navisoma-wslc-probe"

run_once() {
    local n="$1"
    echo "=== run $n: staging ==="
    if [ -e "$STAGING_DIR" ]; then
        echo "FAIL: staging directory already exists before staging (previous run's cleanup incomplete): $STAGING_DIR" >&2
        return 1
    fi
    mkdir -p "$STAGING_DIR"
    cp "$PROBE_EXE" "$STAGING_DIR/probe.exe"
    cp "$WSLCSDK_DLL" "$STAGING_DIR/wslcsdk.dll"
    cp "$IMPORT_FIXTURE" "$STAGING_DIR/import-fixture.tar"
    cp "$BUILD_OUTPUT" "$STAGING_DIR/build-output.tar"

    echo "=== run $n: probe ==="
    local rc=0
    "$STAGING_DIR/probe.exe" || rc=$?
    echo "probe exit code: $rc"

    echo "=== run $n: driver-owned cleanup of staged input files ==="
    rm -f "$STAGING_DIR/probe.exe" "$STAGING_DIR/wslcsdk.dll" "$STAGING_DIR/import-fixture.tar" "$STAGING_DIR/build-output.tar"
    rmdir "$STAGING_DIR" 2>/dev/null || rm -rf "$STAGING_DIR"

    echo "=== run $n: verify parent staging directory gone ==="
    if [ -e "$STAGING_DIR" ]; then
        echo "FAIL: staging directory still exists after cleanup: $STAGING_DIR" >&2
        return 1
    fi
    echo "OK: $STAGING_DIR does not exist"
    return "$rc"
}

overall_rc=0
for i in $(seq 1 "$TIMES"); do
    if ! run_once "$i"; then
        overall_rc=1
    fi
done

exit "$overall_rc"
