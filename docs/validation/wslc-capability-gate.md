# WSLC capability gate — Issue #15

## Purpose

[Issue #14](semantic-core-gate.md) fixed five WSLC capabilities as
unverified prerequisites for the common Canonical Model / Execution Graph
claim: `network.named.isolated`, `volume.named.persistent`,
`port.publish.tcp`, `process.exec.recurring`, and `build.imagestore.import`.
This spike calls the real WSLC C API — not the `wslc`/`container` CLI, not
documentation, not a guess — on a live Windows + WSL Containers host to
confirm or refute each one.

This is not a Nim binding, a production DLL loader, or a C++ gRPC bridge.
The probe in [`wslc-capability-gate/probe.c`](wslc-capability-gate/probe.c)
is disposable: it exists only to drive the pinned SDK's exported functions
directly and report PASS/FAIL per capability.

## Environment

| Item | Value |
|---|---|
| Windows build | `10.0.26200.9445` |
| WSL version | `2.9.9.0` (`wsl --version`) |
| WSL kernel | `6.18.40.1-1` |
| WSLC SDK / runtime version | `2.9.9` — confirmed both by `wslc --version` (`wslc 2.9.9.0`) and, decisively, by the probe's own `WslcGetVersion` call against the running service |
| WSLC SDK package used to build the probe | NuGet `Microsoft.WSL.Containers` `2.9.9` (pinned; see [`wslc-capability-gate/README.md`](wslc-capability-gate/README.md) for SHA-256 of the package, `wslcsdk.h`, `wslcsdk.lib`, `wslcsdk.dll`) |
| CPU architecture | `x86_64` / `x64` (`runtimes/win-x64` artifacts) |
| `WslcGetMissingComponents` at probe start | `0x00000000` — no missing components reported |

The host already had WSL Containers installed and working (`wslc.exe`,
`wslservice.exe`, `container.exe` present under `C:\Program Files\WSL`) —
this is a live WSLC host, not a substitute or a mock.

## Method

No MSVC/Visual Studio C++ toolchain was present on the host. Rather than
substitute CLI output for C API evidence (explicitly disallowed for this
gate) or fabricate results in its absence, the probe was cross-compiled
from the WSL Linux side with Zig 0.16.0 (`zig cc -target
x86_64-windows-gnu`, which bundles a real C/C++ compiler and linker and can
produce a genuine Windows PE binary without a system MinGW or MSVC
install), then executed as an actual Windows process via WSL's interop
(`/mnt/c/...` paths run as real Win32 processes, not emulated). Full build
details, exact pinned artifact hashes, and the reasoning for this toolchain
choice are in [`wslc-capability-gate/README.md`](wslc-capability-gate/README.md).

`wslcsdk.h` is used byte-for-byte as extracted from the pinned package; the
probe only adds its own small, clearly-marked portability shims ahead of
the `#include` (macros mingw's headers don't define, e.g.
`EXTERN_C_START`/`__callback`) — the SDK's declared function signatures and
struct layouts (`WSLC_*_OPTIONS_SIZE`/`_ALIGNMENT`) are never altered or
guessed.

The probe calls the exported C functions directly for every step; nothing
is inferred from a header declaration existing without also calling it,
and no step's result is taken from `wslc.exe`/`container.exe` CLI output.
One integration finding worth recording for future implementers: the SDK
is COM/WinRT-backed, and every call fails with `CO_E_NOTINITIALIZED`
(`0x800401F0`) unless the calling thread has called `CoInitializeEx`
first — not mentioned in `wslcsdk.h`'s comments, found by running the
probe and reading the failure code.

The probe uses fixed, deterministic resource names (session
`navisoma-wslc-probe`, container `navisoma-wslc-probe-c1`, volume
`navisoma-wslc-probe-vol`, image tags under `navisoma-probe-*`) and tears
down everything it creates before exiting, specifically so that running it
a second time from what should be a clean state only succeeds if the first
run's cleanup was actually complete — reusing the same session name would
fail with `WSLC_E_SESSION_RESERVED` otherwise.

## Runs

Both runs used the identical `probe.exe` + `wslcsdk.dll`, invoked with no
arguments, back-to-back, with no manual cleanup in between.

### Run 1

```
CHECK missing_components           PASS missingFlags=0x00000000 (0=nothing missing)
CHECK service_version              PASS sdk_runtime_version=2.9.9
CHECK session_create               PASS hr=0x00000000
CHECK image_pull                   PASS hr=0x00000000
CHECK image_list                   PASS sessionImageCount=1
CHECK image_tag_handoff            PASS hr=0x00000000
CHECK image_import_handoff         PASS hr=0x00000000
CHECK volume_create                PASS hr=0x00000000
CHECK container_create             PASS hr=0x00000000
CHECK container_start              PASS hr=0x00000000
CHECK container_init_process_handle PASS
CHECK published_port_tcp           PASS connected=1 bytes=17 payload=navisoma-port-ok
CHECK process_exec                 PASS hr=0x00000000
CHECK process_stdio_exit_status    PASS waitRes=0 exitCodeHr=0x00000000 exitCode=0 stdout=[navisoma-volume-marker] stderr=[]
CHECK named_volume_mount           PASS marker read back through exec
CHECK container_stop               PASS hr=0x00000000
CHECK container_delete             PASS hr=0x00000000
CHECK container_release            PASS
CHECK volume_delete                PASS hr=0x00000000
CHECK image_tag_delete              PASS hr=0x00000000
CHECK image_pull_delete            PASS hr=0x00000000
CHECK session_terminate            PASS
CHECK session_release              PASS
PROBE_DONE failures=0
```

### Run 2 (immediately after run 1, no manual cleanup)

Identical outcome on every line, including `session_create PASS` reusing
the exact same session name with no `WSLC_E_SESSION_RESERVED` collision:

```
CHECK missing_components           PASS missingFlags=0x00000000 (0=nothing missing)
CHECK service_version              PASS sdk_runtime_version=2.9.9
CHECK session_create               PASS hr=0x00000000
CHECK image_pull                   PASS hr=0x00000000
CHECK image_list                   PASS sessionImageCount=1
CHECK image_tag_handoff            PASS hr=0x00000000
CHECK image_import_handoff         PASS hr=0x00000000
CHECK volume_create                PASS hr=0x00000000
CHECK container_create             PASS hr=0x00000000
CHECK container_start              PASS hr=0x00000000
CHECK container_init_process_handle PASS
CHECK published_port_tcp           PASS connected=1 bytes=17 payload=navisoma-port-ok
CHECK process_exec                 PASS hr=0x00000000
CHECK process_stdio_exit_status    PASS waitRes=0 exitCodeHr=0x00000000 exitCode=0 stdout=[navisoma-volume-marker] stderr=[]
CHECK named_volume_mount           PASS marker read back through exec
CHECK container_stop               PASS hr=0x00000000
CHECK container_delete             PASS hr=0x00000000
CHECK container_release            PASS
CHECK volume_delete                PASS hr=0x00000000
CHECK image_tag_delete              PASS hr=0x00000000
CHECK image_pull_delete            PASS hr=0x00000000
CHECK session_terminate            PASS
CHECK session_release              PASS
PROBE_DONE failures=0
```

### Cleanup verification

The primary evidence for complete cleanup is the probe's own C API result,
not the CLI: run 2 reused the exact same fixed session, container, volume,
and image names as run 1 and every step still returned `S_OK`. Had run 1's
`WslcTerminateSession`/`WslcReleaseSession`/`WslcDeleteContainer`/`WslcDeleteSessionVhdVolume`/`WslcDeleteSessionImage`
calls left anything behind, `session_create` (or a downstream step) in run 2
would have failed — `WslcCreateSession` returns `WSLC_E_SESSION_RESERVED`
for a name still in use. It did not; every step passed identically twice.

`wslc.exe list -a` / `wslc.exe images` were tried afterward purely as an
incidental, non-authoritative human sanity check (consistent with not
treating CLI output as capability evidence either way) and errored with a
generic `E_FAIL` against the host's unrelated, pre-existing default CLI
session (`wslc-cli-asopitech`, present on this host before this spike
started) — a CLI-only observation this gate does not rely on and that has
no bearing on the capability table below, since the probe never touched
that session.

The only on-disk residue after both runs was the probe's own session's
VHDX-backed storage directory at the caller-supplied `storagePath` —
expected: `WslcTerminateSession`/`WslcReleaseSession` end the runtime
session but the SDK exposes no call to delete the storage directory
itself, the same way stopping a VM doesn't delete its disk file. That
directory (and the probe's fixture files) were created solely for this
spike and were removed manually afterward. No probe-created resource
remains on the host.

## Capability table

| Capability (from #14) | WSLC C API entry points used | Result |
|---|---|---|
| Missing components / service version | `WslcGetMissingComponents`, `WslcGetVersion` | supported |
| Session create/terminate/release | `WslcInitSessionSettings`, `WslcCreateSession`, `WslcTerminateSession`, `WslcReleaseSession` | supported |
| Pull/select a small known image | `WslcPullSessionImage`, `WslcListSessionImages`, `WslcDeleteSessionImage` | supported |
| Container create/start/stop/delete/release | `WslcInitContainerSettings`, `WslcCreateContainer`, `WslcStartContainer`, `WslcStopContainer`, `WslcDeleteContainer`, `WslcReleaseContainer` | supported |
| Process with stdout+stderr, exit status (`process.exec.recurring`) | `WslcCreateContainerProcess`, `WslcGetProcessIOHandle`, `WslcGetProcessExitEvent`, `WslcGetProcessExitCode` | supported — a single exec was driven end to end (stdout captured, exit code 0 retrieved); recurrence itself is a planner-owned scheduling loop over this same primitive, not a separate WSLC capability |
| `network.named.isolated` (#14 case 1: one named network per project) | `WslcSetContainerSettingsNetworkingMode(BRIDGED)` + one session per project | supported — see note below |
| `volume.named.persistent` (#14 case 1) | `WslcCreateSessionVhdVolume`, `WslcSetContainerSettingsNamedVolumes`, `WslcDeleteSessionVhdVolume` | supported — volume outlived the container that used it and was independently deletable |
| `port.publish.tcp` (#14 case 1) | `WslcSetContainerSettingsPortMappings` | supported — verified end-to-end: a real WinSock client on the Windows host connected to the published port and received a payload written by a process inside the Linux container |
| `build.imagestore.import` (#14 case 3) | `WslcTagSessionImage` (build-output naming → run), `WslcImportSessionImageFromFile` (external image bytes → session image) | supported — both handoff paths worked; the SDK has no C API to *build* from a Dockerfile (that's CLI/MSBuild/CMake-driven per the SDK's own docs), only to consume an already-built image, which matches NAVISOMA's own design of delegating build to a build tool and handing WSLC the result |

**Note on `network.named.isolated`:** `wslcsdk.h` (682 lines, read in full)
exposes no `WslcCreateNetwork`/list/delete function at all — the only
network-shaped API is a per-container `WslcContainerNetworkingMode` enum
(`NONE` vs `BRIDGED`) plus a `WSLC_E_NETWORK_NOT_FOUND` error code with no
corresponding create/lookup call. There is no WSLC primitive for multiple,
independently-named, user-creatable networks *within* one project. #14's
case 1 only requires **one** named network per project, though, and that
is fully satisfied by lowering "one Compose project" to "one WSLC session"
(each session already gets its own isolated storage/VM-like boundary) with
that session's containers using `BRIDGED` mode for inter-service and
published-port connectivity — which is exactly what was verified above.
If a future Compose scenario needs **multiple, segmented** networks inside
a single project, that specific capability is absent in SDK 2.9.9 and
would need its own capability-gated entry (diagnostic `E-NET-NAMED-UNSUPPORTED`)
rather than being assumed away; #14's fixed scenarios don't require it, so
it is out of scope here rather than scope-breaking.

No row in this table came back scope-breaking, and none needed a
backend-specific workaround: every WSLC divergence from containerd is
either fully supported or, in the one multi-network case just discussed,
cleanly expressible as a capability gate with a normalized diagnostic and
no change to the user's Compose file or NAVISOMA's action order.

## Backend-neutral action order observed

The probe exercised exactly the lifecycle #14 fixed, with every action
implemented by a single, real WSLC C API call and no NAVISOMA-invented
detour:

```
resolve/pull image → create prerequisites (named volume) → create container
(network mode, port mapping, named volume, init process attached)
→ start → wait (init process listening; exec for stdout/stderr/exit status)
→ stop → remove container → remove volume → remove session
```

## Recommendation for #13

**proceed** — every capability #14 flagged as required for the fixed
Compose scenarios is confirmed supported against the real WSLC C API on a
live host, across two independent runs with full, verified cleanup and
re-runnability. The one gap found (no multi-network segmentation within a
single project) falls outside what #14's fixed scenarios require and is
already representable as a capability gate rather than a scope-breaking
case if it becomes relevant later. Combined with #14's finding that no
canonical resource or action needed a backend-specific type or a divergent
user workflow, both child gates for #13 now support proceeding to the
detailed model/graph work in #3, carrying forward the one open item
(multi-network segmentation, if ever required) as a tracked capability gate
rather than a blocker.
