# WSLC lowering: build and live-host verification

## Status

Issue #18 Phase 4 uses the WSLC SDK's direct C projection. `navisoma up --backend wslc` starts a small, same-user daemon that owns the WSLC session; `down` asks that daemon to perform reverse-order cleanup and exit. This is necessary because the SDK has no cross-process session re-attach.

The daemon is compiled into `navisoma.exe` as an internal `--wslc-daemon` mode. Its control pipe is owner-only and project-scoped; no separate service or host-installed toolchain is required.

Issue #22 adds [the WSLC integration workflow](../.github/workflows/wslc-integration.yml).
It is ready to run but is intentionally **unexecuted** until an online Windows
runner carries the `navisoma-wslc` label. Its hosted preflight fails with an
explicit summary when that condition is not met; it never reports a successful
skip. The first live migration-gate run remains the close condition for Issue
#23.

## Operational contract

`NAVISOMA_WSLC_RUNNER_READY` is the administrative admission declaration for
this lane: set it to `true` only after confirming that the labeled runner is
online, and set it to `false` before maintenance. The Ubuntu preflight owns
that gate; the Windows runner owns WSLC execution and cleanup; this runbook
owns the activation procedure. A missing runner declaration or distribution
setting is a mandatory blocker, not an advisory warning or a successful skip.

| Path | Target responsibility | Action | Verification |
| --- | --- | --- | --- |
| `.github/workflows/wslc-integration.yml` | Readiness gate and Windows execution | add | manual dispatch without readiness fails as unexecuted |
| `tests/integration/wslc/run.sh` | Runtime scenario and its own stage cleanup | keep | Windows workflow invokes it unchanged |
| `tests/test_wslc_ci_contract.sh` | Prevent removal of the mandatory gate | add | `nimble test` |
| This runbook | Runner setup and recovery | expand | preflight summary links here |

No prior CI lane is replaced or deleted: `MVP CI` remains the fast Linux
regression check, while this lane is the only owner of live WSLC verification.

## Fixed SDK input

The build container downloads `Microsoft.WSL.Containers` 2.9.9 from NuGet and verifies the package, `wslcsdk.h`, x64 import library, and x64 DLL against the hashes recorded in [`validation/wslc-capability-gate/README.md`](validation/wslc-capability-gate/README.md). The package is not vendored.

`wslcsdk.dll` must be staged next to `navisoma.exe` at runtime. The installed WSL service is a separate prerequisite; the adapter calls `WslcGetMissingComponents` before it creates a session.

## Runner requirements

The dedicated self-hosted runner must satisfy all of the following. Run the
GitHub Actions service as the same normal Windows account that owns the WSL
session; do not use `LocalSystem`, another user's profile, or stored Windows
credentials.

| Requirement | Required value | Verification |
| --- | --- | --- |
| Labels | `self-hosted`, `windows`, `x64`, `navisoma-wslc` | GitHub Actions runner settings show it `online` |
| Windows / WSL | A WSLC-compatible Windows host with WSL `2.9.3` or newer | `wsl.exe --version` and `wsl.exe --status` |
| PowerShell | PowerShell 7 (`pwsh`) | `pwsh --version` |
| Linux distro | Bash, Podman, and access to the Windows checkout through `/mnt/c` | `wsl.exe -d <distro> -- bash -lc 'podman --version'` |
| SDK | The workflow's pinned `Microsoft.WSL.Containers` `2.9.9` | `native/wslc/test-build.sh` hash checks |
| Repository variables | `NAVISOMA_WSLC_WSL_DISTRIBUTION=<distro name>` and `NAVISOMA_WSLC_RUNNER_READY=true` | workflow preflight summary |

The distribution name is a repository variable, not a secret. No Windows
credential, SDK binary, or runner registration token is stored in this
repository.

## Workflow lifecycle

The workflow runs manually with `workflow_dispatch` and weekly on Monday at
18:00 UTC (Tuesday 03:00 JST).

1. A maintainer confirms the labeled runner is online, sets both repository
   variables, then starts the workflow. The Ubuntu preflight rejects a missing
   distribution or a readiness value other than `true` as **WSLC integration is
   unexecuted**. If a declared-ready runner goes offline later, GitHub keeps
   the Windows job queued rather than treating it as successful.
2. Once preflight passes, the dedicated Windows runner records its PowerShell
   and WSL versions, then invokes `tests/integration/wslc/run.sh` inside the
   configured WSL distribution.
3. That script builds the pinned Windows executable and covers healthy
   up/down, successful migration, failed migration, and unhealthy dependency.
4. An `always()` cleanup check fails the job if its staging directory or a
   NAVISOMA daemon remains.

To activate the lane, register and label the runner, set both repository
variables, then use the manual dispatch once. Set readiness back to `false`
before runner maintenance. Keep Issue #22 open until that live run succeeds;
do not change the preflight into a success skip.

## Build and test

Run these from the repository root on the recorded Windows + WSL host:

```bash
native/wslc/test-build.sh
tests/integration/wslc/run.sh
```

The first command builds the Windows x64 executable in a container with Zig, Nim, and the pinned SDK; it writes only ignored files beneath `native/wslc/.build/`. The second stages that executable, DLL, and fixtures on the Windows filesystem, runs healthy up/down and successful migration, then proves failed migration and unhealthy dependency failures do not start their dependents.

## Lifecycle and cleanup

- `up` starts one SDK session per Compose-file identity and leaves it alive so the started containers persist after the CLI process exits.
- `down` reaches the same daemon through its owner-only named pipe, stops/removes exactly the planned services in reverse order, then terminates/releases the session.
- The SDK does not own the caller-provided storage directory. The client removes its project-specific storage after session release; failed `up` follows the same path.
- If the daemon is forcibly killed, the SDK cannot be re-attached from a later process. The command reports that no active session exists rather than claiming cleanup succeeded.

## Deliberately unsupported

This lowering implements only the fixed MVP port: registry image resolution, create/start/stop/remove, and health-command execution. It does not add registry credentials, volumes, ports, GPU, image builds, or a general remote daemon protocol.
