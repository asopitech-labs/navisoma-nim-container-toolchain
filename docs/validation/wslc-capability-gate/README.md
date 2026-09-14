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

### `archive-loader-evidence.tar` — the archive-loading evidence

A real, independently produced, digest-verified multi-file docker-save/OCI
archive, built by [`build_test_image.py`](build_test_image.py) — a
standalone script that talks only to the public `registry-1.docker.io` API
over plain HTTPS (stdlib `urllib`) to fetch `library/busybox` and
re-package it under a new repo:tag. This is **not Dockerfile/BuildKit
build evidence** — no Dockerfile is compiled; an already-published image's
bytes are fetched, verified, and re-tagged. It is real evidence for the
*consumption* half of a Compose `build:` handoff: WSLC's C API only ever
sees bytes in this shape, regardless of what produced them.

```bash
python3 build_test_image.py archive-loader-evidence.tar navisoma-loaded-archive:ci
```

The script pins an immutable manifest digest by default (not a mutable
`latest` tag), verifies every fetched blob — manifest, config, and every
layer — against its own declared content digest before using it, and
prints the final artifact's own digest:

```
source: library/busybox@sha256:1cfa4e2b09e127b9c4ed43578d3f3c18e7d44ea47b9ea98475c0cbe9086525f8
  resolved manifest digest: sha256:1cfa4e2b09e127b9c4ed43578d3f3c18e7d44ea47b9ea98475c0cbe9086525f8
  config digest (verified): sha256:c6348fa86ba0fb2108c9334f5fe913ddc6d853313e655891f133a0127c30099f
  layer digest (verified): sha256:b05093807bb0294152bb9cf86d64da722732dddaf7f8882fa1f120477dbc4db3
new_ref baked into archive manifest.json: navisoma-loaded-archive:ci
wrote archive-loader-evidence.tar (4689920 bytes)
output artifact sha256: fbd4ff7708f0a2650f05fabf374c03c220bfe6b7b94a1454d230d21ee9ada6af
```

Its `manifest.json` carries `"RepoTags": ["navisoma-loaded-archive:ci"]` —
the exact reference the probe expects to see appear in
`WslcListSessionImages` after `WslcLoadSessionImageFromFile` (which takes
no separate name parameter), and the exact reference it then
creates/starts/execs a container from.

## Run

`probe.exe`, `wslcsdk.dll` (from `runtimes/win-x64/native/`), and both
fixtures must sit in the same directory at run time — [`run-probe.sh`](run-probe.sh)
is the reproducible driver that stages them, runs the probe, and owns
cleanup of the staged files afterward (the probe itself owns every WSLC
resource and both caller-owned session storage directories):

```bash
./run-probe.sh <probe.exe> <wslcsdk.dll> <import-fixture.tar> <archive-loader-evidence.tar> 2
```

The probe uses fixed, deterministic resource names across two sessions
(`navisoma-wslc-probe` / `navisoma-wslc-probe-iso`; containers include
`-c1`, `-c2`, `-iso-c`, `-iso-peer`, `-iso-addrbump`, `-iso-posctrl`,
`-rawimport`, `-archive`; volume `navisoma-wslc-probe-vol`; image tags
under `navisoma-probe-*`/`navisoma-loaded-archive`) so that running it
again only succeeds if the previous run's cleanup — including the driver's
own staging-directory cleanup — was complete; the driver itself refuses to
stage over a directory that still exists. See
[`../wslc-capability-gate.md`](../wslc-capability-gate.md) for recorded
runs and the resulting capability table.

## Known WSLC behavior found while building this probe

Each session's bridge allocates container addresses independently,
starting from the same base (e.g. `172.17.0.2`) regardless of what
addresses are already in use in a different, concurrently running session.
A freshly created session's first container can therefore collide,
address-string-for-address-string, with an unrelated container in another
session — this was caught by the probe's own
`cross_session_ip_addresses_distinct` check, which failed on the first
attempt at the cross-session isolation test before a same-session
"address bump" container (kept running, not just created-and-deleted --
deleting one immediately frees its address back to the pool for instant
reuse) was added to force the real peer container onto a different
address. This is a real behavior worth knowing about for the eventual
IP-allocation design of a WSLC backend, not just a probe artifact.
