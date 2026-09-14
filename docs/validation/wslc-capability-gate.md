# WSLC capability gate — Issue #15

## Purpose

[Issue #14](semantic-core-gate.md) fixes the WSLC capabilities required by
Case 1's Canonical Model: `network.named`, `volume.named.persistent`,
`port.publish.tcp`, `process.exec.recurring`, and `build.imagestore.import`
(see #14's scope note on `network.named` — cross-project network isolation
and single-project multi-network segmentation are explicit future platform
promises, not part of this checklist). This spike calls the real WSLC C
API — not the `wslc`/`container` CLI, not documentation, not a guess — on
a live Windows + WSL Containers host to confirm or refute each one.

**This document records WSLC adapter capability evidence. It is not, by
itself, a proof that NAVISOMA's own backend-neutral Canonical Model /
Execution Graph is irreducible — that is Issue #13's question, decided
from #14's semantic-model analysis together with evidence like this.**

This is not a Nim binding, a production DLL loader, or a C++ gRPC bridge.
The probe in [`wslc-capability-gate/probe.c`](wslc-capability-gate/probe.c)
is disposable: it exists only to drive the pinned SDK's exported functions
directly and report PASS/FAIL per capability.

This document has been through several rounds of correction. The
capabilities below reflect defects that were real and specific to them
(stdout-only assertions where stderr was never checked; an image import
that was never followed by actually running the imported image) — those
fixes are described in place. An earlier round also carried a
cross-session/cross-project network isolation claim; per #13's scope
decision, that claim is out of scope for this gate (see #14's scope note)
and has been removed from this document and from the probe, not merely
marked `unverified`.

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

Two independently-sourced fixtures are used — neither produced by WSLC,
BuildKit, or any tool this spike validates:

- `import-fixture.tar` — a single statically linked `busybox` binary
  fetched over plain HTTPS from `busybox.net`, packaged as a flat
  single-layer rootfs. **Supplementary evidence only**: it proves
  `WslcImportSessionImageFromFile` accepts and can run externally-supplied
  bytes, but a flat unnamed rootfs is not what a Compose `build:` produces,
  so it is not used as build-handoff evidence.
- `archive-loader-evidence.tar` — a real, multi-file docker-save/OCI
  archive built by
  [`build_test_image.py`](wslc-capability-gate/build_test_image.py), a
  standalone script that fetches `library/busybox` from the public Docker
  Hub registry by **pinned, immutable manifest digest** (not a mutable
  tag), **verifies every blob it uses against its own declared content
  digest**, and re-packages it under a new repo:tag
  (`navisoma-loaded-archive:ci`) baked into the archive's own
  `manifest.json`. This is explicitly **not** described as Dockerfile/
  BuildKit build evidence — no Dockerfile is compiled here, only an
  already-published image's bytes are fetched, verified, and re-tagged.
  It is genuine evidence for the *consumption* half of a Compose build
  handoff, since WSLC's C API only ever sees bytes in this shape
  regardless of what produced them. See capability 5 below and the README
  for exact digests.

The `WslcInspectContainer` JSON schema is undocumented in `wslcsdk.h`.
Rather than guess a field name for a container's own IP address, the probe
was first run with the raw inspect payload printed to find the real shape:
`"NetworkSettings":{"Networks":{"bridge":{"Gateway":"172.17.0.1",...,"IPAddress":"172.17.0.3",...}}}`
— confirming a naive "first dotted-quad number in the text" scan would
have silently picked the bridge's gateway instead of the container's own
address (it did, on the first attempt). The probe looks up the named
`IPAddress` field specifically because of that observed payload, not a
guess.

The probe uses fixed, deterministic resource names in **one** session
(`navisoma-wslc-probe`) and tears down every resource it creates before
exiting — including the session's caller-owned storage directory, deleted
and verified gone via `GetFileAttributesW`, not merely trusted.
[`run-probe.sh`](wslc-capability-gate/run-probe.sh) is the reproducible
external driver that stages the probe binary and fixtures, runs the probe,
then owns cleanup of the staged files and verifies the parent staging
directory itself no longer exists.

## Policy compliance

[`docs/validation/work-instruction-policy.md`](work-instruction-policy.md)
governs validation work under `docs/validation/`. Its rules on negative
claims (setup/measurement completion, the documented negative result, a
positive control, and source/target identity evidence) are why the
cross-session isolation material that used to live in this document was
removed rather than patched further: per #13's scope decision (see #14's
scope note on `network.named`), that claim is not part of this gate's
required-capability checklist at all, so no amount of predicate-tightening
on it belongs here.

## Runs

Both runs were made through [`run-probe.sh`](wslc-capability-gate/run-probe.sh),
which stages `probe.exe` + `wslcsdk.dll` + both fixtures fresh each time,
runs the probe, then deletes the staged files and verifies the parent
staging directory is gone. The lines below are the genuine subset of two
previously-captured, real host runs' output — filtered to the capability
checks that still exist in the current probe after removing the
cross-session material; no line below was fabricated or re-run for this
revision.

### Run 1 (surviving checks only)

```
CHECK missing_components           PASS missingFlags=0x00000000 (0=nothing missing)
CHECK service_version              PASS sdk_runtime_version=2.9.9
CHECK session_create               PASS hr=0x00000000
CHECK image_pull                   PASS hr=0x00000000
CHECK image_list                   PASS sessionImageCount=1
CHECK image_tag_handoff            PASS hr=0x00000000
CHECK supplementary_raw_rootfs_import PASS hr=0x00000000
CHECK supplementary_raw_rootfs_run_verify PASS created=1 started=1 waitedOk=1 exitOk=1 exitCode=0 stdout=[navisoma-import-stdout-marker] stderr=[navisoma-import-stderr-marker]
CHECK supplementary_raw_rootfs_container_delete_postcondition PASS
CHECK supplementary_raw_rootfs_image_delete PASS hr=0x00000000
CHECK supplementary_raw_rootfs_image_delete_postcondition PASS listHr=0x00000000 stillPresent=0
CHECK archive_load_handoff         PASS hr=0x00000000
CHECK archive_load_exact_reference_observed PASS listHr=0x00000000 count=3 nameObserved=1
CHECK archive_load_run_verify      PASS created=1 started=1 waitedOk=1 exitOk=1 exitCode=0 stdout=[navisoma-archive-stdout-marker] stderr=[navisoma-archive-stderr-marker]
CHECK archive_load_container_delete_postcondition PASS
CHECK archive_load_image_delete    PASS hr=0x00000000
CHECK archive_load_image_delete_postcondition PASS listHr=0x00000000 stillPresent=0
CHECK volume_create                PASS hr=0x00000000
CHECK container_create             PASS hr=0x00000000
CHECK container_start              PASS hr=0x00000000
CHECK container_init_process_handle PASS
CHECK published_port_tcp           PASS connected=1 bytes=17 payload=navisoma-port-ok
CHECK process_exec                 PASS
CHECK process_stdio_exit_status    PASS waitedOk=1 exitOk=1 exitCode=0 stdout=[navisoma-volume-marker] stderr=[navisoma-exec-stderr-marker]
CHECK process_exec_second_invocation PASS execOk=1 waitedOk=1 exitOk=1 exitCode=0 stdout=[navisoma-exec2-stdout-marker] stderr=[navisoma-exec2-stderr-marker]
CHECK peer_container_ip_inspect    PASS 172.17.0.3
CHECK intra_session_service_connectivity PASS peerIp=172.17.0.3 waitRes=0 stdout=[navisoma-peer-ok]
CHECK peer_container_delete_postcondition PASS reopen hr=0x80040603 (expected FAILED = truly deleted)
CHECK container_stop               PASS hr=0x00000000
CHECK container_termination_signal_verified PASS exitEventWait=0 state=2 exitCodeHr=0x00000000 exitCode=137
CHECK container_delete             PASS hr=0x00000000
CHECK container_release            PASS
CHECK container_delete_postcondition PASS reopen hr=0x80040603 (expected FAILED = truly deleted)
CHECK volume_delete                PASS hr=0x00000000
CHECK volume_delete_postcondition  PASS second delete hr=0x80040604 (expected FAILED = truly deleted)
CHECK image_tag_delete             PASS hr=0x00000000
CHECK image_tag_delete_postcondition PASS listHr=0x00000000 tagStillPresent=0
CHECK image_pull_delete            PASS hr=0x00000000
CHECK image_pull_delete_postcondition PASS listHr=0x00000000 countAfter=0 alpineStillPresent=0
CHECK session_terminate            PASS
CHECK session_release              PASS
CHECK session_storage_deleted_and_verified_gone PASS shFileOpRc=0 attrsAfter=0xFFFFFFFF (INVALID_FILE_ATTRIBUTES=gone)
```

### Run 2 (surviving checks only, via the same driver, immediately after)

Identical on every line, including `session_create` succeeding again with
no `WSLC_E_SESSION_RESERVED`; full transcript omitted since it is
byte-for-byte identical to Run 1's filtered output above.

**After this revision removed the cross-session code, the probe was
rebuilt and statically compiled (`zig cc -target x86_64-windows-gnu`,
clean, no errors, no new warnings) to confirm it still builds. It was not
re-run against the host — the run transcripts above are the genuine
prior host observations for the capabilities that survive, not a new run.**

## Cleanup verification

Every resource the (now single-session) probe creates is enumerated below
with its own delete call and its own observed postcondition — not a
trusted return code:

| Resource | Delete call | Postcondition check | Observed |
|---|---|---|---|
| Container `c1` | `WslcDeleteContainer` | `WslcOpenContainer` by name expected to fail | `WSLC_E_CONTAINER_NOT_FOUND` (`0x80040603`) |
| Container `c2` (peer) | `WslcDeleteContainer` | `WslcOpenContainer` by name expected to fail | `WSLC_E_CONTAINER_NOT_FOUND` (`0x80040603`) |
| Container (raw-rootfs one-shot) | `WslcDeleteContainer` | `WslcOpenContainer` by name expected to fail | `deleteVerifiedGone` — pass |
| Container (archive-load one-shot) | `WslcDeleteContainer` | `WslcOpenContainer` by name expected to fail | `deleteVerifiedGone` — pass |
| Image: pulled `alpine` | `WslcDeleteSessionImage` | `WslcListSessionImages` re-list expected to omit it | `countAfter=0` |
| Image: tag `navisoma-probe-worker:ci` | `WslcDeleteSessionImage` | re-list expected to omit it | `tagStillPresent=0` |
| Image: raw-rootfs import | `WslcDeleteSessionImage` | re-list expected to omit it | `stillPresent=0` |
| Image: loaded archive `navisoma-loaded-archive:ci` | `WslcDeleteSessionImage` | re-list expected to omit it | `stillPresent=0` |
| Volume | `WslcDeleteSessionVhdVolume` | Second delete of the same name expected to fail | `WSLC_E_VOLUME_NOT_FOUND` (`0x80040604`) |
| Session | `WslcTerminateSession` + `WslcReleaseSession` | Reusing the exact same session name in the next run expected to succeed | Succeeded both runs |
| Session's `storagePath` (caller-owned) | `SHFileOperationW` (recursive delete, driven by the probe itself) | `GetFileAttributesW` expected to return `INVALID_FILE_ATTRIBUTES` | Confirmed both runs |
| Staged input files (`probe.exe`, `wslcsdk.dll`, both fixture tars) | `rm -f` in `run-probe.sh` (driver-owned) | Parent staging directory expected to not exist | Confirmed both runs |

`wslc.exe list -a` / `wslc.exe images` were tried afterward purely as an
incidental, non-authoritative human sanity check and showed no containers
or images — not relied on as evidence for any row above.

**Production implication:** `storagePath` is caller-supplied in
`WslcInitSessionSettings`, so the caller-owned storage directory above is
caller-owned by construction, not a WSLC gap to route around. A NAVISOMA
WSLC backend must delete that directory itself (after a successful
`WslcReleaseSession`, or as part of recovering a session that will never
be reused) the same way it already owns cleanup for any other backend's
on-disk state.

## WSLC adapter capability evidence

Each row below is real-machine evidence that a specific WSLC C API
mechanism works as described. **None of these rows are evidence for or
against #13's question of whether NAVISOMA needs its own backend-neutral
semantic core** — they show what the WSLC adapter layer can rely on,
which is a necessary input to that question but not the answer to it.

### 1. `process.exec.recurring`

| Field | Content |
|---|---|
| Claim | A process can be exec'd into a running WSLC container, its stdout and stderr can both be read, its exit code retrieved, and the same mechanism invoked again against the same container. |
| Stimulus | `WslcCreateContainerProcess` with `/bin/sh -c "cat <volume file>; echo navisoma-exec-stderr-marker 1>&2"`, then a second, independent `WslcCreateContainerProcess` with distinct markers, against the same container. |
| Oracle | `WslcGetProcessIOHandle` reads on both `STDOUT`/`STDERR` contain their respective distinct markers, `WslcGetProcessExitCode` returns `0`, **jointly** (one combined pass condition), for *both* invocations. |
| Cleanup owner | The exec'd process itself is released via `WslcReleaseProcess`; no independent resource survives it. |
| Boundary | Proves the primitive a scheduler would call repeatedly, not a real scheduler loop with intervals/retries/timeouts. |
| **Observed** | Both invocations passed jointly on both runs (see transcript). |

### 2. `network.named` (#14 case 1: one named network per project, intra-project connectivity)

| Field | Content |
|---|---|
| Claim | Lowering "one Compose project" to "one WSLC session" with `BRIDGED` networking gives that project's services mutual reachability. |
| Stimulus | A second container (`c2`) in the *same* session as `c1`, also `BRIDGED`, running an `nc` listener on an internal port (9000, no host mapping); `c1` execs `nc -w 3 <c2's IP> 9000`. |
| Oracle | `c2`'s own IP read from `WslcInspectContainer`'s `IPAddress` field; `c1`'s exec stdout contains `navisoma-peer-ok`. |
| Cleanup owner | SDK, via `WslcDeleteContainer`; postcondition checked (`peer_container_delete_postcondition`). |
| Boundary | Intra-project connectivity within one session only. Whether a different, independently created session/project can or cannot reach this one is explicitly out of scope for this gate — see #14's scope note on `network.named` — and is not addressed by this row or anywhere else in this document. |
| **Observed** | `intra_session_service_connectivity` passed on both runs. |

### 3. `volume.named.persistent` (#14 case 1)

| Field | Content |
|---|---|
| Claim | A named volume created independently of any container can be mounted into a container, retains content written to it, and can be deleted independently once no container needs it. |
| Stimulus | `WslcCreateSessionVhdVolume` before any container exists; `WslcSetContainerSettingsNamedVolumes` attaches it to `c1` at `/mnt/probe-vol`; `c1`'s init process writes a marker file into it; a separate exec (`cat`) reads it back; `WslcDeleteSessionVhdVolume` after `c1` is already deleted. |
| Oracle | The exec's stdout contains `navisoma-volume-marker`; the delete call returns `S_OK`; a second delete of the same name then fails. |
| Cleanup owner | SDK, via `WslcDeleteSessionVhdVolume`; postcondition checked (`0x80040604` `WSLC_E_VOLUME_NOT_FOUND` on the repeat delete). |
| Boundary | One volume, one project/session. Sharing one named volume *across* two different sessions was not tested. |
| **Observed** | `volume_delete_postcondition` passed on both runs. |

### 4. `port.publish.tcp` (#14 case 1)

| Field | Content |
|---|---|
| Claim | A container port can be published to a fixed Windows host port and reached from outside the container's network namespace. |
| Stimulus | `WslcSetContainerSettingsPortMappings` maps host `18080` → container `80`; `c1`'s init process loops `echo navisoma-port-ok \| nc -l -p 80`; a real WinSock client on the Windows host connects to `127.0.0.1:18080`. |
| Oracle | `recv` returns 17 bytes containing `navisoma-port-ok`. |
| Cleanup owner | The socket is closed by the probe itself; the container's port mapping ends when the container is deleted. |
| Boundary | One port, TCP only (UDP mapping exists in the API but was not exercised). |
| **Observed** | `published_port_tcp` passed on both runs. |

### 5. `build.imagestore.import` (#14 case 3)

| Field | Content |
|---|---|
| Claim | The full handoff chain — an image produced independently of WSLC, transferred in with its producer-assigned identity intact and every blob verified, and consumed by a container create/start/exec — works. |
| Stimulus | [`build_test_image.py`](wslc-capability-gate/build_test_image.py) fetches `library/busybox` from the public Docker Hub registry **by pinned manifest digest**, **verifies the config blob and every layer blob against their declared digests**, and writes a real docker-save/OCI archive whose `manifest.json` carries `"RepoTags": ["navisoma-loaded-archive:ci"]`. `WslcLoadSessionImageFromFile` — which takes **no separate name parameter** — loads it; `WslcListSessionImages` is checked for that exact name; a container is created, started, and exec'd **from that exact name**. |
| Oracle | `archive_load_exact_reference_observed`: the archive's own embedded name appears in the session's image list after loading. `archive_load_run_verify`: a container created from that exact name reaches exit code `0` with both a stdout and a distinct stderr marker present. |
| Cleanup owner | SDK: `WslcDeleteContainer`/`WslcReleaseContainer` for the one-shot container, `WslcDeleteSessionImage` for the loaded image, with a re-list postcondition. |
| Boundary | This is explicitly **not** Dockerfile/BuildKit build evidence — no Dockerfile is compiled; only an already-published image's bytes are fetched (with every declared digest verified) and re-tagged. |
| **Observed** | Both `archive_load_exact_reference_observed` and `archive_load_run_verify` passed on both runs. |

## Baseline SDK lifecycle (supporting evidence)

| Operation | WSLC C API entry points used | Cleanup owner (postcondition) | Observed |
|---|---|---|---|
| Missing components / service version | `WslcGetMissingComponents`, `WslcGetVersion` | n/a — query only, no resource created | Passed both runs |
| Session create/terminate/release | `WslcInitSessionSettings`, `WslcCreateSession`, `WslcTerminateSession`, `WslcReleaseSession` | SDK owns the runtime session; caller owns the `storagePath` directory | Passed both runs |
| Pull a small known image | `WslcPullSessionImage`, `WslcListSessionImages` | SDK, via `WslcDeleteSessionImage`; postcondition checked | Passed both runs |
| Container create/start/stop/delete/release | `WslcInitContainerSettings`, `WslcCreateContainer`, `WslcStartContainer`, `WslcStopContainer`, `WslcDeleteContainer`, `WslcReleaseContainer` | SDK, via `WslcDeleteContainer`; postcondition checked | Passed both runs |
| Long-running process termination | `WslcStopContainer` (SIGTERM) on `c1`'s genuinely long-running init loop, then `WslcGetProcessState`/`WslcGetProcessExitEvent`/`WslcGetProcessExitCode` on its init process | n/a — verification, not a resource | `state=2` (`WSLC_PROCESS_STATE_EXITED`), exit code `137` (`128+SIGKILL`): the shell loop did not exit cleanly on SIGTERM within the grace period and WSLC escalated to SIGKILL, reported as observed rather than assumed graceful |

## Out of scope for this gate

Per #13's scope decision (see #14's scope note on `network.named`):
cross-project network isolation and single-project multi-network
segmentation are future platform promises, not part of this gate's
required-capability checklist. Neither is addressed by this document or
by the current probe. If either is taken up as its own separately-fixed
scenario in the future, it will need its own proof design, reviewed before
implementation, per `work-instruction-policy.md`.

## Status

This document records WSLC adapter capability evidence for the five
capabilities #14 (as revised) requires. It does not itself conclude
anything about #13. #13's question — whether NAVISOMA needs its own
backend-neutral Canonical Model / Execution Graph — is decided from #14's
semantic-model analysis together with evidence like this, not by this
document alone. **#13 and #15 remain pending.**
