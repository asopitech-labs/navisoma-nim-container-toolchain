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
  <extracted-nupkg>/runtimes/win-x64/wslcsdk.lib -lws2_32 -lole32
```

`probe.exe` and `wslcsdk.dll` (from `runtimes/win-x64/native/`) must sit in
the same directory at run time; `wslcsdk.dll` is not preinstalled elsewhere
on the host, only the higher-level `wslc.exe`/`wslservice.exe` are.

## Run

The probe takes no arguments and uses fixed, deterministic resource names
(session `navisoma-wslc-probe`, container `navisoma-wslc-probe-c1`, volume
`navisoma-wslc-probe-vol`, image tags under `navisoma-probe-*`) so that
running it twice in a row only succeeds the second time if the first run's
cleanup was complete — see
[`../wslc-capability-gate.md`](../wslc-capability-gate.md) for the two
recorded runs and the resulting capability table.
