# WSLC capability probe — build notes

This directory contains only the disposable probe's source
([`probe.c`](probe.c)). It is not part of the NAVISOMA product; it exists to
call the pinned WSLC SDK C API directly on a live Windows + WSL Containers
host for [Issue #15](../wslc-capability-gate.md).

## Pinned SDK

Fetched from NuGet, not vendored into this repository:

- Package: `Microsoft.WSL.Containers` version `2.9.9`
- Source: `https://api.nuget.org/v3-flatcontainer/microsoft.wsl.containers/2.9.9/microsoft.wsl.containers.2.9.9.nupkg`
- Package SHA-256: `7764355a4fa3ca29d3c8a1dc2a0f4ee1c9c4603d110a20218aef5cd2efa90fe2`
- `include/wslcsdk.h` SHA-256: `e2867c46e51db62b7307dc858c2f5cd767d298760b272ad19a161145bd1bbba1`
- `runtimes/win-x64/native/wslcsdk.dll` SHA-256: `8d4d55d4283fb32a5909b57e78b576d01363d7b28bb9b2595115e80faf61db5b`
- `runtimes/win-x64/wslcsdk.lib` SHA-256: `c2a1023c8f97253c478b5e97df609898410534111cae3086f3eee9a5942bd526`

This version was chosen because it matches the installed WSL/WSLC runtime
build (`wslc --version` reports `2.9.9.0`) on the probe host.

`wslcsdk.h` itself is used byte-for-byte as extracted; `probe.c` only adds
its own small, clearly-marked portability shims (`EXTERN_C_START`/`END`,
`__callback`) ahead of `#include "wslcsdk.h"` because the header is written
against MSVC/WDK headers and this probe was cross-compiled.

## Build

No MSVC/Visual Studio C++ toolchain was available on the host at probe
time, so the probe was cross-compiled from the WSL Linux side with
[Zig](https://ziglang.org/) 0.16.0 (`zig cc`, which bundles `clang` + `lld`
and can target `x86_64-windows-gnu` without a system MinGW install), then
executed as a genuine Windows process via WSL interop:

```bash
zig cc -target x86_64-windows-gnu \
  -I <extracted-nupkg>/include \
  probe.c -o probe.exe \
  <extracted-nupkg>/runtimes/win-x64/wslcsdk.lib -lws2_32 -lole32 -lshell32
```

`probe.exe` and `wslcsdk.dll` (from `runtimes/win-x64/native/`) must sit in
the same directory at run time; `wslcsdk.dll` is not preinstalled elsewhere
on the host, only the higher-level `wslc.exe`/`wslservice.exe` are.

## Fixtures

Two fixtures must be present at run time, at fixed Windows paths under
`%TEMP%\navisoma-wslc-probe\`. Neither is produced by WSLC, BuildKit, or
any tool this spike validates.

### `import-fixture.tar` — supplementary, not build-handoff evidence

A minimal but genuinely runnable Linux root filesystem — a single
statically linked `busybox` binary at `/bin/busybox` — built from a binary
fetched over plain HTTPS from
`https://busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox`
(SHA-256 `6e123e7f3202a8c1e9b1f94d8941580a25135382b99e8d3e34fb858bba31134`):

```bash
mkdir -p rootfs-fixture/bin
cp busybox rootfs-fixture/bin/busybox
chmod 755 rootfs-fixture/bin/busybox
tar --owner=0 --group=0 -C rootfs-fixture -cf import-fixture.tar bin
```

Resulting tar SHA-256: `682263d1c30309ffa85fd5ddfab8a189a4763c907edc0f9e563a1eed7bc9e60e`.
This is a flat, single-layer, unnamed rootfs (`WslcImportSessionImageFromFile`'s
"docker import"-shaped semantics) — real evidence that the SDK can accept
and run externally-supplied bytes, but *not* evidence for the Compose
`build:` handoff claim, since a real build produces a multi-layer,
self-naming image, not a bare rootfs tar. It is also reused, unmodified,
by the cross-session isolation check (a session needs *some* runnable
image, and this is the smallest one already on hand).

### `build-output.tar` — the actual build-handoff evidence

A real, independently produced multi-file docker-save/OCI archive, built
by [`build_test_image.py`](build_test_image.py) — a standalone script that
talks only to the public `registry-1.docker.io` API over plain HTTPS
(stdlib `urllib`) to fetch `library/busybox` and re-package it under a
new repo:tag:

```bash
python3 build_test_image.py build-output.tar navisoma-build-output:ci
```

This resolved `library/busybox:latest` to config digest
`sha256:c6348fa86ba0fb2108c9334f5fe913ddc6d853313e655891f133a0127c30099f`
at build time (re-run the script to track a newer `latest`, or edit
`SOURCE_TAG` to pin a specific tag). Resulting tar SHA-256:
`4150b81df6056e0d61cc2ec9c12ded98f662f67b5fcd2e7bd589ab006147278e`. Its
`manifest.json` carries `"RepoTags": ["navisoma-build-output:ci"]` — the
exact reference the probe expects to see appear in `WslcListSessionImages`
after `WslcLoadSessionImageFromFile`, and the exact reference it then
creates/starts/execs a container from.

## Run

`probe.exe`, `wslcsdk.dll` (from `runtimes/win-x64/native/`), and both
fixtures must sit in the same directory at run time — [`run-probe.sh`](run-probe.sh)
is the reproducible driver that stages them, runs the probe, and owns
cleanup of the staged files afterward (the probe itself owns every WSLC
resource and both caller-owned session storage directories):

```bash
./run-probe.sh <probe.exe> <wslcsdk.dll> <import-fixture.tar> <build-output.tar> 2
```

The probe uses fixed, deterministic resource names (sessions
`navisoma-wslc-probe`/`navisoma-wslc-probe-iso`, containers
`navisoma-wslc-probe-c1`/`-c2`/`-iso-c`/`-rawimport`/`-buildout`, volume
`navisoma-wslc-probe-vol`, image tags under `navisoma-probe-*`/`navisoma-build-output`)
so that running it again only succeeds if the previous run's cleanup —
including the driver's own staging-directory cleanup — was complete; the
driver itself refuses to stage over a directory that still exists. See
[`../wslc-capability-gate.md`](../wslc-capability-gate.md) for recorded
runs and the resulting capability table.
