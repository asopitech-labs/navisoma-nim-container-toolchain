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

This document has been reopened and corrected twice by static review
before this version, each time for a real, specific defect rather than a
style objection:

1. Assertions checked stdout only, import was never followed by actually
   running the imported image, and the network-isolation claim rested on
   one directed test alone.
2. The isolation claim was still only tested in one direction with no
   isolation-specific evidence, cleanup left several resources unverified
   by anything but their own delete call's return code, and the
   build-handoff evidence was a raw single-file rootfs rather than a real
   build archive.
3. The isolation test's actual pass predicate (`!reachedC1`) was weaker
   than its documented oracle — it passed on a timeout, a broken client, or
   an unretrievable exit status, not only on a confirmed negative result;
   it tested one direction with no positive control and no check that the
   two sessions' addresses were even distinct; two image deletes still had
   no absence postcondition; and the archive fixture repackaged a mutable
   `latest` tag without verifying blob digests or recording the final
   artifact's own digest.

Round 3's fixes are described in place below, alongside a genuinely
interesting finding surfaced while implementing them: the two sessions'
bridges independently allocated the *same* address to two different
containers, which would have silently invalidated the isolation test had
the new identity check not caught it. See "Cross-session address
collision" under capability 2.

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
  so it is not used as build-handoff evidence. It is also reused, in a
  separate session, as the general-purpose runnable image for the
  cross-session isolation checks (which need *some* image in that second
  session, not this specific one).
- `archive-loader-evidence.tar` — a real, multi-file docker-save/OCI
  archive built by
  [`build_test_image.py`](wslc-capability-gate/build_test_image.py), a
  standalone script that fetches `library/busybox` from the public Docker
  Hub registry by **pinned, immutable manifest digest** (not a mutable
  tag), **verifies every blob it uses against its own declared content
  digest**, and re-packages it under a new repo:tag
  (`navisoma-loaded-archive:ci`) baked into the archive's own
  `manifest.json`. This is explicitly **not** described as Dockerfile/
  BuildKit build evidence anywhere in this probe or its docs — no
  Dockerfile is compiled here, only an already-published image's bytes are
  fetched, verified, and re-tagged. It is genuine evidence for the
  *consumption* half of a Compose build handoff, since WSLC's C API only
  ever sees bytes in this shape regardless of what produced them. See
  capability 5 below and the README for exact digests.

The `WslcInspectContainer` JSON schema is undocumented in `wslcsdk.h`.
Rather than guess a field name for a container's own IP address, the probe
was first run with the raw inspect payload printed to find the real shape:
`"NetworkSettings":{"Networks":{"bridge":{"Gateway":"172.17.0.1",...,"IPAddress":"172.17.0.3",...}}}`
— confirming a naive "first dotted-quad number in the text" scan would
have silently picked the bridge's gateway instead of the container's own
address (it did, on the first attempt). The probe looks up the named
`IPAddress` field specifically because of that observed payload, not a
guess.

The probe uses fixed, deterministic resource names across **two**
independent sessions (main: `navisoma-wslc-probe`; isolation-test:
`navisoma-wslc-probe-iso`) and tears down every resource it creates before
exiting — including both sessions' caller-owned storage directories,
deleted and verified gone via `GetFileAttributesW`, not merely trusted.
[`run-probe.sh`](wslc-capability-gate/run-probe.sh) is the reproducible
external driver that stages the probe binary and fixtures, runs the probe,
then owns cleanup of the staged files and verifies the parent staging
directory itself no longer exists.

## Policy compliance

[`docs/validation/work-instruction-policy.md`](work-instruction-policy.md)
governs validation work under `docs/validation/`, and was itself hardened
(commit `f12f144`) in direct response to defects found in the previous
version of this document: negative claims must assert setup/measurement
completion, the documented negative result, a positive control, and
source/target identity evidence; every oracle must trace to one boolean
predicate in the probe, not a diagnostic string or a different row's
check; external artifacts must be pinned to an immutable digest with every
blob verified; and "complete cleanup" must cover every created resource,
including supplementary fixtures and secondary sessions. This round's
changes were made to satisfy that hardened policy specifically, not just
its earlier draft.

## Runs

Both runs were made through [`run-probe.sh`](wslc-capability-gate/run-probe.sh),
which stages `probe.exe` + `wslcsdk.dll` + both fixtures fresh each time,
runs the probe, then deletes the staged files and verifies the parent
staging directory is gone — refusing to even start a run if that directory
still exists from a previous one. Both runs' raw output is identical
except for the fixed bridge IPs the allocator happened to assign (also
identical between the two runs: `172.17.0.2` for `c1`/the address-bump
container, `172.17.0.3` for `c2`/the iso peer).

### Run 1

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
CHECK c1_container_ip_inspect      PASS 172.17.0.2
CHECK published_port_tcp           PASS connected=1 bytes=17 payload=navisoma-port-ok
CHECK process_exec                 PASS
CHECK process_stdio_exit_status    PASS waitedOk=1 exitOk=1 exitCode=0 stdout=[navisoma-volume-marker] stderr=[navisoma-exec-stderr-marker]
CHECK process_exec_second_invocation PASS execOk=1 waitedOk=1 exitOk=1 exitCode=0 stdout=[navisoma-exec2-stdout-marker] stderr=[navisoma-exec2-stderr-marker]
CHECK peer_container_ip_inspect    PASS 172.17.0.3
CHECK intra_session_service_connectivity PASS peerIp=172.17.0.3 waitRes=0 stdout=[navisoma-peer-ok]
CHECK peer_container_delete_postcondition PASS reopen hr=0x80040603 (expected FAILED = truly deleted)
CHECK iso_session_create           PASS hr=0x00000000
CHECK iso_session_image_import     PASS hr=0x00000000
CHECK iso_session_address_allocator_bump PASS bumpIp=172.17.0.2
CHECK iso_peer_container_create    PASS
CHECK iso_peer_container_start     PASS
CHECK iso_peer_container_ip_inspect PASS 172.17.0.3
CHECK cross_session_ip_addresses_distinct PASS c1Ip=172.17.0.2 isoPeerIp=172.17.0.3
CHECK cross_session_positive_control PASS target=172.17.0.3:9000 created=1 started=1 waitedOk=1 exitOk=1 exitCode=0 stdout=[navisoma-iso-peer-ok]
CHECK cross_session_isolation_iso_to_main PASS target=172.17.0.2:80 completed=1 exitCode=1 stdout=[] (expected: completed=1, no navisoma-port-ok)
CHECK iso_to_main_container_delete_postcondition PASS
CHECK cross_session_isolation_main_to_iso PASS target=172.17.0.3:9000 completed=1 exitCode=1 stdout=[] (expected: completed=1, no navisoma-iso-peer-ok)
CHECK iso_peer_container_delete_postcondition PASS
CHECK iso_session_address_allocator_bump_delete_postcondition PASS
CHECK iso_session_image_delete     PASS
CHECK iso_session_image_delete_postcondition PASS listHr=0x00000000 stillPresent=0
CHECK iso_session_terminate        PASS
CHECK iso_session_release          PASS
CHECK iso_session_storage_deleted_and_verified_gone PASS shFileOpRc=0 attrsAfter=0xFFFFFFFF (INVALID_FILE_ATTRIBUTES=gone)
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
PROBE_DONE failures=0
```

Driver: `=== run 1: verify parent staging directory gone === OK: ...navisoma-wslc-probe does not exist`

### Run 2 (via the same driver, immediately after)

Identical on every line, including every bridge IP, `iso_session_create`
succeeding again with no `WSLC_E_SESSION_RESERVED`, and the driver again
confirming the parent staging directory gone at the end. (61/61 checks
pass both runs; full transcript omitted here since it is byte-for-byte
identical to Run 1 above.)

## Cleanup verification

Every resource the probe creates is enumerated below with its own delete
call and its own observed postcondition — not a trusted return code:

| Resource | Delete call | Postcondition check | Observed |
|---|---|---|---|
| Container `c1` | `WslcDeleteContainer` | `WslcOpenContainer` by name expected to fail | `WSLC_E_CONTAINER_NOT_FOUND` (`0x80040603`) |
| Container `c2` (peer) | `WslcDeleteContainer` | `WslcOpenContainer` by name expected to fail | `WSLC_E_CONTAINER_NOT_FOUND` (`0x80040603`) |
| Container (raw-rootfs one-shot) | `WslcDeleteContainer` | `WslcOpenContainer` by name expected to fail | `deleteVerifiedGone` — pass |
| Container (archive-load one-shot) | `WslcDeleteContainer` | `WslcOpenContainer` by name expected to fail | `deleteVerifiedGone` — pass |
| Container (iso session's address-bump) | `WslcDeleteContainer` | `WslcOpenContainer` by name expected to fail | pass |
| Container (iso session's peer) | `WslcDeleteContainer` | `WslcOpenContainer` by name expected to fail | pass |
| Container (iso session's positive-control one-shot) | `WslcDeleteContainer` | `WslcOpenContainer` by name expected to fail | `deleteVerifiedGone` — pass |
| Container (iso session's iso→main one-shot) | `WslcDeleteContainer` | `WslcOpenContainer` by name expected to fail | `deleteVerifiedGone` — pass |
| Image: pulled `alpine` | `WslcDeleteSessionImage` | `WslcListSessionImages` re-list expected to omit it | `countAfter=0` |
| Image: tag `navisoma-probe-worker:ci` | `WslcDeleteSessionImage` | re-list expected to omit it | `tagStillPresent=0` |
| Image: raw-rootfs import (main session) | `WslcDeleteSessionImage` | re-list expected to omit it | `stillPresent=0` |
| Image: loaded archive `navisoma-loaded-archive:ci` | `WslcDeleteSessionImage` | re-list expected to omit it | `stillPresent=0` |
| Image: raw-rootfs import (iso session) | `WslcDeleteSessionImage` | re-list expected to omit it | `stillPresent=0` |
| Volume | `WslcDeleteSessionVhdVolume` | Second delete of the same name expected to fail | `WSLC_E_VOLUME_NOT_FOUND` (`0x80040604`) |
| Main session | `WslcTerminateSession` + `WslcReleaseSession` | Reusing the exact same session name in the next run expected to succeed | Succeeded both runs |
| Main session's `storagePath` (caller-owned) | `SHFileOperationW` (recursive delete, driven by the probe itself) | `GetFileAttributesW` expected to return `INVALID_FILE_ATTRIBUTES` | Confirmed both runs |
| Iso session | `WslcTerminateSession` + `WslcReleaseSession` | Reusing the exact same session name in the next run expected to succeed | Succeeded both runs |
| Iso session's `storagePath` (caller-owned) | `SHFileOperationW`, by the probe | `GetFileAttributesW` expected to return `INVALID_FILE_ATTRIBUTES` | Confirmed both runs |
| Staged input files (`probe.exe`, `wslcsdk.dll`, both fixture tars) | `rm -f` in `run-probe.sh` (driver-owned) | Parent staging directory expected to not exist | Confirmed both runs |

Every image and every container this probe creates, in both sessions,
now has its own re-open/re-delete/re-list postcondition — this closes the
previous round's gap where the two raw-rootfs images (main and iso
sessions) had only their delete call's own `S_OK` as evidence.

`wslc.exe list -a` / `wslc.exe images` were tried afterward purely as an
incidental, non-authoritative human sanity check and showed no containers
or images — the probe never leaves anything for the CLI to see, though
this is not relied on as evidence for any row above.

**Production implication:** `storagePath` is caller-supplied in
`WslcInitSessionSettings`, so the caller-owned storage directories above
are caller-owned by construction, not a WSLC gap to route around. A
NAVISOMA WSLC backend must delete that directory itself (after a
successful `WslcReleaseSession`, or as part of recovering a session that
will never be reused) the same way it already owns cleanup for any other
backend's on-disk state.

## Capability proof contracts (#14's five required capabilities)

### 1. `process.exec.recurring`

| Field | Content |
|---|---|
| Claim | A process can be exec'd into a running WSLC container, its stdout and stderr can both be read, its exit code retrieved, and the same mechanism invoked again against the same container. |
| Stimulus | `WslcCreateContainerProcess` with `/bin/sh -c "cat <volume file>; echo navisoma-exec-stderr-marker 1>&2"`, then a second, independent `WslcCreateContainerProcess` with distinct markers, against the same container. |
| Oracle | `WslcGetProcessIOHandle` reads on both `STDOUT`/`STDERR` contain their respective distinct markers, `WslcGetProcessExitCode` returns `0`, **jointly** (one combined pass condition), for *both* invocations. This exact joint predicate lives in `process_stdio_exit_status`'s own `logResult` call — traceable to one boolean expression, not split across rows. |
| Negative condition | Either invocation's stderr or stdout buffer missing its marker, a non-zero/unavailable exit code, or the second invocation failing outright. |
| Consumer proof | This same exec mechanism is what `volume.named.persistent` (below) relies on to read back its marker file. |
| Cleanup owner | The exec'd process itself is released via `WslcReleaseProcess`; no independent resource survives it to clean up. |
| Boundary | Proves the primitive a scheduler would call repeatedly, not a real scheduler loop with intervals/retries/timeouts — that logic is planner-owned, not a WSLC capability. |
| **Result** | **supported** |

### 2. `network.named.isolated` (#14 case 1: one named network per project)

This capability has two distinct halves, each with its own CHECK and its
own oracle: services in the same project must reach each other, and a
different project must not reach them in either direction.

| Field | Content |
|---|---|
| Claim (2a: intra-project) | Lowering "one Compose project" to "one WSLC session" with `BRIDGED` networking gives that project's services mutual reachability. |
| Stimulus (2a) | A second container (`c2`) in the *same* session as `c1`, also `BRIDGED`, running an `nc` listener on an internal port (9000, no host mapping); `c1` execs `nc -w 3 <c2's IP> 9000`. |
| Oracle (2a) | `c2`'s own IP read from `WslcInspectContainer`'s `IPAddress` field; `c1`'s exec stdout contains `navisoma-peer-ok`. Predicate: `intra_session_service_connectivity`'s own `connectOk`. |
| Negative condition (2a) | `c1`'s exec times out, returns nothing, or connects to the wrong address. |
| Consumer proof (2a) | `c1`, a separate container in the same project, is the actual consumer of `c2`'s service. |
| Claim (2b: cross-project isolation) | A container in a **different, independently created session**, alive concurrently, cannot reach a container in the first session, **and the reverse direction also fails**. |
| Stimulus (2b) | A second session (`navisoma-wslc-probe-iso`); a peer container in it (`iso-peer`, long-running, `BRIDGED`) as the bidirectional target; a one-shot iso-session container execs `nc -w 3 <c1's IP> 80` (direction iso→main); `c1` execs `nc -w 3 <iso-peer's IP> 9000` (direction main→iso). |
| Oracle (2b) | For **each** direction: the exec must actually *complete* (`created && started && waitedOk && exitOk`, or the exec-into-running-container equivalent) **and** the target's specific marker must be absent from stdout — marker-absence alone is not the predicate; an incomplete/timed-out/errored exec is logged as `SKIP`, not `PASS`. Predicates: `cross_session_isolation_iso_to_main`, `cross_session_isolation_main_to_iso`. |
| Negative condition (2b) | Either direction's exec succeeding and printing the target's marker (would mean no isolation), or either direction failing to complete at all (logged as inconclusive, never counted as a pass). |
| Positive control (2b) | Before either isolation direction runs, the *same* client mechanism (imported image, one-shot container, `nc -w 3 <ip> <port>`) is pointed at `iso-peer` from **within its own session** and must succeed (`cross_session_positive_control`). The two isolation directions above only run, and only count, if this passed — a silently broken client/toolchain would otherwise look identical to genuine isolation. |
| Identity evidence (2b) | `cross_session_ip_addresses_distinct` explicitly asserts `c1`'s and `iso-peer`'s IP address strings differ before either isolation direction is interpreted — see "Cross-session address collision" below for why this check exists and what it initially caught. |
| Consumer proof (2b) | The iso-session container's exec (direction 1) and `c1`'s exec (direction 2) are the actual connecting parties, not inspection alone. |
| Cleanup owner | SDK for every container/image involved (each independently postcondition-checked — see "Cleanup verification"); SDK + caller for both sessions and their storage directories, same table. |
| Boundary | Two sessions, one bidirectional pair of connection attempts. Concurrent *load* across many sessions, and isolation under session crash/recovery, were not tested. |
| **Result** | **supported** — both halves have direct, passing, completion-gated, positive-controlled, identity-checked evidence, addressing every element the hardened policy requires for a negative claim. |

**Cross-session address collision.** The first version of this round's
isolation test computed `c1Ip != isoPeerIp` and got a **collision**: both
were `172.17.0.2`. Each session's bridge allocates addresses independently
starting from the same base, with no awareness of what another
concurrently running session is already using. With that collision, the
"isolation" result would have been uninterpretable — connecting to
`172.17.0.2:80` from inside the iso session could have meant "genuinely
blocked from reaching the other session" or could have meant "nothing
local is listening at my own session's identically-numbered address 80",
and those look identical from the outside. A one-shot create-then-delete
"bump" container was tried first to shift the allocator and did not work:
the freed address was handed straight back out to the very next container
created. The fix that worked was keeping a bump container **running**
(physically occupying the address) until after the real peer container
was created, which then received a different address (`172.17.0.3`),
confirmed distinct by the identity check before either isolation
direction was interpreted. This is recorded here because it is a real
WSLC behavior relevant to a future backend's own IP-allocation design, not
only a probe artifact — see the README's "Known WSLC behavior" section.

`WslcInspectContainer`'s JSON schema is undocumented in `wslcsdk.h`. A
first attempt scanned the payload for "the first dotted-quad-looking
substring" and got the bridge's *gateway*, not the container's own
address, because `"Gateway"` sorts before `"IPAddress"` in the emitted
JSON. This was caught by printing the real payload rather than trusting
the heuristic; the probe now looks up the named `IPAddress` field.

### 3. `volume.named.persistent` (#14 case 1)

| Field | Content |
|---|---|
| Claim | A named volume created independently of any container can be mounted into a container, retains content written to it, and can be deleted independently once no container needs it. |
| Stimulus | `WslcCreateSessionVhdVolume` before any container exists; `WslcSetContainerSettingsNamedVolumes` attaches it to `c1` at `/mnt/probe-vol`; `c1`'s init process writes a marker file into it; a separate exec (`cat`) reads it back; `WslcDeleteSessionVhdVolume` after `c1` is already deleted. |
| Oracle | The exec's stdout contains `navisoma-volume-marker` (part of capability 1's joint pass condition); the delete call returns `S_OK`; a second delete of the same name then fails. |
| Negative condition | The marker missing from the exec's stdout, or the second delete also succeeding. |
| Consumer proof | The `cat` exec — a process distinct from the one that wrote the file — is the consumer of the mounted content. |
| Cleanup owner | SDK, via `WslcDeleteSessionVhdVolume`; postcondition checked (`0x80040604` `WSLC_E_VOLUME_NOT_FOUND` on the repeat delete). |
| Boundary | One volume, one project/session. Sharing one named volume *across* two different sessions was not tested. |
| **Result** | **supported** |

### 4. `port.publish.tcp` (#14 case 1)

| Field | Content |
|---|---|
| Claim | A container port can be published to a fixed Windows host port and reached from outside the container's network namespace. |
| Stimulus | `WslcSetContainerSettingsPortMappings` maps host `18080` → container `80`; `c1`'s init process loops `echo navisoma-port-ok \| nc -l -p 80`; a real WinSock client (`socket`/`connect`/`recv`, not WSLC API) on the Windows host connects to `127.0.0.1:18080`. |
| Oracle | `recv` returns 17 bytes containing `navisoma-port-ok`. |
| Negative condition | `connect` failing, or `recv` returning 0 bytes/different content. |
| Consumer proof | The Windows-host TCP client *is* the consumer — this is the same relationship Compose's `ports:` exists to serve. |
| Cleanup owner | The socket is closed by the probe itself (plain WinSock); the container's port mapping ends when the container is deleted (`container_delete_postcondition`). |
| Boundary | One port, TCP only (UDP mapping exists in the API but was not exercised). |
| **Result** | **supported** |

### 5. `build.imagestore.import` (#14 case 3)

| Field | Content |
|---|---|
| Claim | The full handoff chain — an image produced independently of WSLC, transferred in with its producer-assigned identity intact and every blob verified, and consumed by a container create/start/exec — works. |
| Stimulus | [`build_test_image.py`](wslc-capability-gate/build_test_image.py) fetches `library/busybox` from the public Docker Hub registry **by pinned manifest digest**, **verifies the config blob and every layer blob against their declared digests**, and writes a real docker-save/OCI archive whose `manifest.json` carries `"RepoTags": ["navisoma-loaded-archive:ci"]` and whose own final artifact digest is recorded. `WslcLoadSessionImageFromFile` — which takes **no separate name parameter** — loads it; `WslcListSessionImages` is checked for that exact name; a container is created, started, and exec'd **from that exact name**, not a name the probe supplied. |
| Oracle | `archive_load_exact_reference_observed`: the archive's own embedded name appears in the session's image list after loading. `archive_load_run_verify`: a container created from that exact name reaches exit code `0` with both a stdout and a distinct stderr marker present. |
| Negative condition | The name failing to appear after load, or the container failing to create/start/exec from it. |
| Consumer proof | The one-shot container's own exec output is the consumer of the loaded image; the raw-rootfs-import test is explicitly demoted to supplementary evidence for a narrower claim ("the SDK can run *something* handed to it as bytes") and is never used for this row. |
| Cleanup owner | SDK: `WslcDeleteContainer`/`WslcReleaseContainer` for the one-shot container, `WslcDeleteSessionImage` for the loaded image, with a re-list postcondition. |
| Boundary | This is explicitly **not** Dockerfile/BuildKit build evidence — no Dockerfile is compiled; only an already-published image's bytes are fetched (with every declared digest verified), and re-tagged. It is real evidence for the *consumption* side of the handoff contract, since WSLC's C API only ever sees bytes in this shape regardless of what produced them. One layer, one archive; a multi-layer image was not additionally tested (the source image happens to be single-layer). |
| **Result** | **supported** |

## Baseline SDK lifecycle (supporting evidence)

| Operation | WSLC C API entry points used | Consumer proof | Cleanup owner (postcondition) | Result |
|---|---|---|---|---|
| Missing components / service version | `WslcGetMissingComponents`, `WslcGetVersion` | Would gate WSLC backend eligibility before scheduling any deployment | n/a — query only, no resource created | supported |
| Session create/terminate/release (×2 sessions) | `WslcInitSessionSettings`, `WslcCreateSession`, `WslcTerminateSession`, `WslcReleaseSession` | Every other operation in each session's lifetime uses that session's handle | SDK owns the runtime session; caller owns the `storagePath` directory | supported |
| Pull a small known image | `WslcPullSessionImage`, `WslcListSessionImages` | `image_tag_handoff` and `c1`'s container create both reference the pulled image | SDK, via `WslcDeleteSessionImage`; postcondition checked | supported |
| Container create/start/stop/delete/release (×8 containers across 2 sessions) | `WslcInitContainerSettings`, `WslcCreateContainer`, `WslcStartContainer`, `WslcStopContainer`, `WslcDeleteContainer`, `WslcReleaseContainer` | Every check under "Capability proof contracts" runs against one of these containers | SDK, via `WslcDeleteContainer`; postcondition checked for each (see "Cleanup verification") | supported |
| Long-running process termination | `WslcStopContainer` (SIGTERM) on `c1`'s genuinely long-running init loop, then `WslcGetProcessState`/`WslcGetProcessExitEvent`/`WslcGetProcessExitCode` on its init process | Confirms `c1` actually terminates before its container is deleted, not just that the stop call returned `S_OK` | n/a — verification, not a resource | supported — `state=2` (`WSLC_PROCESS_STATE_EXITED`), exit code `137` (`128+SIGKILL`): the shell loop did not exit cleanly on SIGTERM within the grace period and WSLC escalated to SIGKILL, reported as observed rather than assumed graceful |

## Discovered boundaries

One limitation remains genuinely out of scope for #14's three fixed,
single-project scenarios:

| Boundary | Result | Why |
|---|---|---|
| Multiple, independently-named, user-creatable networks *within* one project | **capability-gated: absent in WSLC SDK 2.9.9** | Confirmed by reading the full 682-line `wslcsdk.h`: no `WslcCreateNetwork`/list/delete function exists — only a per-container `WslcContainerNetworkingMode` enum (`NONE`/`BRIDGED`). No live capability-query API exists for this either, so a NAVISOMA WSLC backend would need a static per-pinned-SDK-version capability declaration, not a runtime probe, to refuse this explicitly. |

Cross-session network isolation, the other boundary an earlier round left
`unverified`, now has direct, bidirectional, positive-controlled,
identity-checked evidence under capability 2 above and is no longer listed
here as a boundary.

This one remaining boundary is not scope-breaking: it is cleanly
expressible as a capability gate with no change to the user's Compose file
or NAVISOMA's action order.

## Backend-neutral action order observed

```
resolve/pull image → tag (build-output naming) → create prerequisites (named volume)
→ create container (network mode, port mapping, named volume, init process attached)
→ start → wait (init process listening; exec ×2 for stdout/stderr/exit status and
  recurrence; a second same-session container + exec for peer-to-peer connectivity;
  a container in a second, concurrently alive session attempting -- and failing,
  bidirectionally, with a positive control proving the test mechanism itself works --
  to reach this one)
→ stop (with termination verified via exit event/state/code) → remove container
  (+ postcondition lookup) → remove volume (+ postcondition delete) → remove session
  (+ caller-owned storage deleted and verified gone)
```

Several one-shot flows run the same create → start → wait(exit) → stop →
remove shape independently of the long-running main container: the
supplementary raw-rootfs import, the archive load-handoff, and (inside the
iso session) the address-bump, positive-control, and iso-to-main isolation
containers.

## Recommendation for #13

Per explicit instruction, this document does not itself conclude
`proceed` for #13, and the live-host results above have not been
independently reproduced by anyone other than the author of this round's
fixes. What can be said plainly: every specific defect raised against the
previous two rounds — weak-marker-only assertions, import-without-
consumption, single-direction isolation with no positive control, an
unnoticed address collision, missing cleanup postconditions, and a mutable
unverified archive source — is closed with passing, postcondition-verified
evidence from two independent, fully reproducible runs via `run-probe.sh`.
No row in this document is `unverified`; the one remaining
`capability-gated` row (multi-network segmentation within a project) is a
confirmed, not merely suspected, absence that #14's fixed scenarios do not
require.

**This issue's status is left as pending — not proceed — for independent
review to confirm before #13 acts on it.** If that confirmation holds, the
substance of what would be proposed is: proceed, carrying forward the one
remaining boundary (multi-network segmentation) as a tracked capability
gate rather than a blocker, consistent with #14's finding that no
canonical resource or action needed a backend-specific type or a divergent
user workflow.
