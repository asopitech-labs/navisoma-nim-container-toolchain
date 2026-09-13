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

The import-handoff fixture (`import-fixture.tar`) is a minimal but genuinely
runnable Linux root filesystem: a single statically linked `busybox`
binary at `/bin/busybox`, fetched over plain HTTPS from
`busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox`
(SHA-256 `6e123e7f3202a8c1e9b1f94d8941580a25135382b99e8d3e34fb858bba31134`) —
independent of WSLC, BuildKit, and any container runtime, so the fixture's
own provenance can't be mistaken for evidence about the capability it's
used to test. Its tar SHA-256 is `682263d1c30309ffa85fd5ddfab8a189a4763c907edc0f9e563a1eed7bc9e60e`.

The `WslcInspectContainer` JSON schema is undocumented in `wslcsdk.h`.
Rather than guess a field name for a container's own IP address (needed
for the intra-session connectivity check below), the probe was first run
with the raw inspect payload printed to find the real shape:
`"NetworkSettings":{"Networks":{"bridge":{"Gateway":"172.17.0.1",...,"IPAddress":"172.17.0.3",...}}}`
— confirming a naive "first dotted-quad number in the text" scan would
have silently picked the bridge's gateway instead of the container's own
address (it did, on the first attempt). The probe looks up the named
`IPAddress` field specifically because of that observed payload, not a
guess.

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
CHECK image_import_run_verify      PASS created=1 started=1 waitedOk=1 exitOk=1 exitCode=0 stdout=[navisoma-import-stdout-marker] stderr=[navisoma-import-stderr-marker]
CHECK volume_create                PASS hr=0x00000000
CHECK container_create             PASS hr=0x00000000
CHECK container_start              PASS hr=0x00000000
CHECK container_init_process_handle PASS
CHECK published_port_tcp           PASS connected=1 bytes=17 payload=navisoma-port-ok
CHECK process_exec                 PASS hr=0x00000000
CHECK process_stdio_exit_status    PASS waitRes=0 exitCodeHr=0x00000000 exitCode=0 stdout=[navisoma-volume-marker] stderr=[navisoma-exec-stderr-marker]
CHECK named_volume_mount           PASS marker read back through exec
CHECK peer_container_ip_inspect    PASS 172.17.0.3
CHECK intra_session_service_connectivity PASS peerIp=172.17.0.3 waitRes=0 stdout=[navisoma-peer-ok]
CHECK container_stop               PASS hr=0x00000000
CHECK container_delete             PASS hr=0x00000000
CHECK container_release            PASS
CHECK volume_delete                PASS hr=0x00000000
CHECK image_tag_delete             PASS hr=0x00000000
CHECK image_pull_delete            PASS hr=0x00000000
CHECK session_terminate            PASS
CHECK session_release              PASS
PROBE_DONE failures=0
```

### Run 2 (immediately after run 1, no manual cleanup)

Identical outcome on every line, including `session_create PASS` reusing
the exact same session name with no `WSLC_E_SESSION_RESERVED` collision,
and the peer container getting the exact same bridge IP again:

```
CHECK missing_components           PASS missingFlags=0x00000000 (0=nothing missing)
CHECK service_version              PASS sdk_runtime_version=2.9.9
CHECK session_create               PASS hr=0x00000000
CHECK image_pull                   PASS hr=0x00000000
CHECK image_list                   PASS sessionImageCount=1
CHECK image_tag_handoff            PASS hr=0x00000000
CHECK image_import_handoff         PASS hr=0x00000000
CHECK image_import_run_verify      PASS created=1 started=1 waitedOk=1 exitOk=1 exitCode=0 stdout=[navisoma-import-stdout-marker] stderr=[navisoma-import-stderr-marker]
CHECK volume_create                PASS hr=0x00000000
CHECK container_create             PASS hr=0x00000000
CHECK container_start              PASS hr=0x00000000
CHECK container_init_process_handle PASS
CHECK published_port_tcp           PASS connected=1 bytes=17 payload=navisoma-port-ok
CHECK process_exec                 PASS hr=0x00000000
CHECK process_stdio_exit_status    PASS waitRes=0 exitCodeHr=0x00000000 exitCode=0 stdout=[navisoma-volume-marker] stderr=[navisoma-exec-stderr-marker]
CHECK named_volume_mount           PASS marker read back through exec
CHECK peer_container_ip_inspect    PASS 172.17.0.3
CHECK intra_session_service_connectivity PASS peerIp=172.17.0.3 waitRes=0 stdout=[navisoma-peer-ok]
CHECK container_stop               PASS hr=0x00000000
CHECK container_delete             PASS hr=0x00000000
CHECK container_release            PASS
CHECK volume_delete                PASS hr=0x00000000
CHECK image_tag_delete             PASS hr=0x00000000
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

**Production implication:** cleanup here is not fully automatic, and this
is not a WSLC defect to work around — `storagePath` is caller-supplied in
`WslcInitSessionSettings`, so it is caller-owned storage by construction.
A NAVISOMA WSLC backend must treat deleting that directory (after a
successful `WslcReleaseSession`, or as part of recovering from a session
that will never be reused) as its own explicit responsibility, the same
way it already owns cleanup for any other backend's on-disk state. This
should be written down as a lowering-adapter obligation for the WSLC
backend in the detailed model/graph work, not left implicit.

## Capability table

| Capability (from #14) | WSLC C API entry points used | Result |
|---|---|---|
| Missing components / service version | `WslcGetMissingComponents`, `WslcGetVersion` | supported |
| Session create/terminate/release | `WslcInitSessionSettings`, `WslcCreateSession`, `WslcTerminateSession`, `WslcReleaseSession` | supported |
| Pull/select a small known image | `WslcPullSessionImage`, `WslcListSessionImages`, `WslcDeleteSessionImage` | supported |
| Container create/start/stop/delete/release | `WslcInitContainerSettings`, `WslcCreateContainer`, `WslcStartContainer`, `WslcStopContainer`, `WslcDeleteContainer`, `WslcReleaseContainer` | supported |
| Process with stdout+stderr, exit status (`process.exec.recurring`) | `WslcCreateContainerProcess`, `WslcGetProcessIOHandle`, `WslcGetProcessExitEvent`, `WslcGetProcessExitCode` | supported — one exec (`cat <volume file>; echo ... 1>&2`) asserted stdout content, a *distinct* stderr marker, and exit code 0 together, not stdout alone; recurrence itself is a planner-owned scheduling loop over this same primitive, not a separate WSLC capability |
| `network.named.isolated` (#14 case 1: one named network per project) | `WslcSetContainerSettingsNetworkingMode(BRIDGED)` + one session per project | supported — see note below |
| `volume.named.persistent` (#14 case 1) | `WslcCreateSessionVhdVolume`, `WslcSetContainerSettingsNamedVolumes`, `WslcDeleteSessionVhdVolume` | supported — volume outlived the container that used it and was independently deletable |
| `port.publish.tcp` (#14 case 1) | `WslcSetContainerSettingsPortMappings` | supported — verified end-to-end: a real WinSock client on the Windows host connected to the published port and received a payload written by a process inside the Linux container |
| `build.imagestore.import` (#14 case 3) | `WslcTagSessionImage` (build-output naming → run), `WslcImportSessionImageFromFile` (external image bytes → session image) | supported — see note below |

**Note on `process.exec.recurring` / stderr:** the first version of this
probe's exec check only ran `/bin/cat <file>`, which produces no stderr at
all, and the check passed on stdout and exit code alone — silently proving
nothing about the stderr channel one way or the other. This was caught in
review before being treated as evidence. The exec now runs
`/bin/sh -c "cat <file>; echo navisoma-exec-stderr-marker 1>&2"` and the
check requires the distinct stderr marker to be present in addition to the
stdout content and exit code 0.

**Note on `build.imagestore.import`:** the first version of this probe
imported the fixture image via `WslcImportSessionImageFromFile` and then
deleted it immediately, which only proved the SDK *accepted* externally-
supplied bytes, not that WSLC could actually run a container from them —
also caught in review. The probe now creates a container from the
imported image, runs its init process to completion, and asserts its
stdout marker, a distinct stderr marker, and exit code 0 (`image_import_run_verify`
above) before deleting it. Separately, `WslcTagSessionImage` retags the
already-pulled `alpine` image and that tag is what the main container (`c1`)
actually runs from, so the tag-based handoff path is exercised by the rest
of the probe's own checks rather than being immediately discarded too.
The SDK has no C API to *build* from a Dockerfile at all (that's
CLI/MSBuild/CMake-driven per the SDK's own docs) — only to consume an
already-built image — which matches NAVISOMA's own design of delegating
build elsewhere and handing WSLC the result.

**Note on `network.named.isolated`:** `wslcsdk.h` (682 lines, read in full)
exposes no `WslcCreateNetwork`/list/delete function at all — the only
network-shaped API is a per-container `WslcContainerNetworkingMode` enum
(`NONE` vs `BRIDGED`) plus a `WSLC_E_NETWORK_NOT_FOUND` error code with no
corresponding create/lookup call. There is no WSLC primitive for multiple,
independently-named, user-creatable networks *within* one project. #14's
case 1 only requires **one** named network per project, and it needs that
network to do two things: let the project's own services reach each
other, and let the published port reach the host. Both were verified
directly, not assumed: the published-port path is described above, and a
**second container (`c2`) was created in the same session**, its own IP
address read back from `WslcInspectContainer` (`172.17.0.3` in both runs —
found by first printing the raw inspect payload rather than guessing the
JSON field name, since the payload also contains the bridge's `Gateway`
address as a similar-looking string), and `c1` successfully exec'd a
client that connected to `c2` over that IP with no host port involved.
That is the concrete evidence for lowering "one Compose project" to "one
WSLC session" with `BRIDGED` mode as the network for that project.

What this spike did **not** test is isolation *between* two different
sessions/projects — #14's three fixed scenarios are all single-project, so
that claim was out of scope here and is not asserted; it's an
architecturally-plausible consequence of each session getting its own
VM-like boundary, not a verified one. Multi-network segmentation *within*
a single project is likewise absent from SDK 2.9.9 and out of scope for
what #14 required. Either would need its own capability-gated entry
(e.g. diagnostic `E-NET-NAMED-UNSUPPORTED`/`E-NET-ISOLATION-UNVERIFIED`) if
a future scenario needs it, rather than being assumed away.

No row in this table came back scope-breaking, and none needed a
backend-specific workaround: every WSLC divergence from containerd is
either fully supported or, in the two out-of-scope cases just discussed,
cleanly expressible as a capability gate with a normalized diagnostic and
no change to the user's Compose file or NAVISOMA's action order.

## Backend-neutral action order observed

The probe exercised exactly the lifecycle #14 fixed, with every action
implemented by a single, real WSLC C API call and no NAVISOMA-invented
detour:

```
resolve/pull image → tag (build-output naming) → create prerequisites (named volume)
→ create container (network mode, port mapping, named volume, init process attached)
→ start → wait (init process listening; exec for stdout/stderr/exit status;
  a second same-session container + exec for peer-to-peer connectivity)
→ stop → remove container → remove volume → remove session
```

The import-handoff path (`image_import_handoff` / `image_import_run_verify`)
runs the same create → start → wait(exit) → stop → remove shape as a
one-shot container, independently of the long-running main container above.

## Recommendation for #13

**proceed** — every capability #14 flagged as required for the fixed
Compose scenarios is confirmed supported against the real WSLC C API on a
live host, across two independent runs with full, verified cleanup and
re-runnability. This includes closing the two gaps a static review of an
earlier version of this evidence correctly flagged as insufficient: the
exec check now asserts a distinct stderr marker rather than stdout alone,
and the import-handoff path now creates/starts/execs a container from the
imported image and verifies its output before deleting it, rather than
importing and immediately discarding it. The `network.named.isolated` claim
is now backed by an actual second container in the same session reaching
the first one over its own IP, not just the published-port test alone.
The remaining gaps — multi-network segmentation within one project, and
isolation *between* two sessions/projects — both fall outside what #14's
three fixed scenarios require (all three are single-project) and are
already representable as capability gates rather than scope-breaking cases
if a future scenario needs them. Combined with #14's finding that no
canonical resource or action needed a backend-specific type or a divergent
user workflow, both child gates for #13 now support proceeding to the
detailed model/graph work in #3, carrying forward these two open items as
tracked capability gates rather than blockers.
