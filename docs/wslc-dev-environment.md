# WSLC lowering: build and live-host verification

## Status

Issue #18 Phase 4 uses the WSLC SDK's direct C projection. `navisoma up --backend wslc` starts a small, same-user daemon that owns the WSLC session; `down` asks that daemon to perform reverse-order cleanup and exit. This is necessary because the SDK has no cross-process session re-attach.

The daemon is compiled into `navisoma.exe` as an internal `--wslc-daemon` mode. Its control pipe is owner-only and project-scoped; no separate service or host-installed toolchain is required.

## Fixed SDK input

The build container downloads `Microsoft.WSL.Containers` 2.9.9 from NuGet and verifies the package, `wslcsdk.h`, x64 import library, and x64 DLL against the hashes recorded in [`validation/wslc-capability-gate/README.md`](validation/wslc-capability-gate/README.md). The package is not vendored.

`wslcsdk.dll` must be staged next to `navisoma.exe` at runtime. The installed WSL service is a separate prerequisite; the adapter calls `WslcGetMissingComponents` before it creates a session.

## Build and test

Run these from the repository root on the recorded Windows + WSL host:

```bash
native/wslc/test-build.sh
tests/integration/wslc/run.sh
```

The first command builds the Windows x64 executable in a container with Zig, Nim, and the pinned SDK; it writes only ignored files beneath `native/wslc/.build/`. The second stages that executable, DLL, and fixtures on the Windows filesystem, runs successful `up`/`down`, then proves an unhealthy dependency fails before its dependent can start.

## Lifecycle and cleanup

- `up` starts one SDK session per Compose-file identity and leaves it alive so the started containers persist after the CLI process exits.
- `down` reaches the same daemon through its owner-only named pipe, stops/removes exactly the planned services in reverse order, then terminates/releases the session.
- The SDK does not own the caller-provided storage directory. The client removes its project-specific storage after session release; failed `up` follows the same path.
- If the daemon is forcibly killed, the SDK cannot be re-attached from a later process. The command reports that no active session exists rather than claiming cleanup succeeded.

## Deliberately unsupported

This lowering implements only the fixed MVP port: registry image resolution, create/start/stop/remove, and health-command execution. It does not add registry credentials, volumes, ports, GPU, image builds, or a general remote daemon protocol.
