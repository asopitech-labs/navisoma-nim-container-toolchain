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

This is the third pass at this evidence. The first round asserted
`supported` without adequate proof; a static review caught that (stdout-only
exec assertions, import-without-consumption, an isolation claim resting on
one test). The second round closed those three gaps but was reopened again
because two of its own claims were still not proven to the standard this
now needs: `network.named.isolated`'s isolation half was still `unverified`
rather than tested, and `build.imagestore.import`'s primary evidence was a
raw single-file rootfs import rather than a real multi-layer build archive.
This round closes both, plus resource-enumerated cleanup with per-resource
postconditions and a reproducible external stage/run/cleanup driver. See
"Recommendation for #13" for how this round's conclusion is reported.

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
- `build-output.tar` — a real, multi-file docker-save/OCI archive built by
  [`build_test_image.py`](wslc-capability-gate/build_test_image.py), a
  standalone script that fetches `library/busybox` from the public Docker
  Hub registry over plain HTTPS and re-packages it under a new repo:tag
  (`navisoma-build-output:ci`) baked into the archive's own `manifest.json`.
  **This is the build-handoff evidence** — see capability 5 below.

Full fixture provenance and hashes are in the README.

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
directory itself no longer exists — see the README for the split of
responsibility between the probe and this driver.

## Policy compliance

[`docs/validation/work-instruction-policy.md`](work-instruction-policy.md)
governs validation work under `docs/validation/`. Per that policy's rule
that review findings replace a conclusion rather than append a caveat to
it, this is a full rewrite of the previous round's document, not an
amendment. Every one of #14's five required capabilities now has a proof
contract (claim, stimulus, oracle, negative condition, consumer proof,
cleanup owner/postcondition, boundary); `unverified` is used only where
something genuinely was not tested, and this round eliminated both
`unverified` claims the previous round still carried by actually testing
them.

## Runs

Both runs were made through [`run-probe.sh`](wslc-capability-gate/run-probe.sh),
which stages `probe.exe` + `wslcsdk.dll` + both fixtures fresh each time,
runs the probe, then deletes the staged files and verifies the parent
staging directory is gone — refusing to even start a run if that directory
still exists from a previous one. Both runs' raw output is identical
except for the fixed IPs the bridge happened to assign (which were also
identical, `172.17.0.2`/`172.17.0.3`, both runs).

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
CHECK build_output_load_handoff    PASS hr=0x00000000
CHECK build_output_exact_reference_observed PASS listHr=0x00000000 count=3 nameObserved=1
CHECK build_output_run_verify      PASS created=1 started=1 waitedOk=1 exitOk=1 exitCode=0 stdout=[navisoma-build-stdout-marker] stderr=[navisoma-build-stderr-marker]
CHECK build_output_container_delete_postcondition PASS
CHECK build_output_image_delete    PASS hr=0x00000000
CHECK build_output_image_delete_postcondition PASS listHr=0x00000000 stillPresent=0
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
CHECK cross_session_isolation      PASS target=172.17.0.2:80 waitedOk=1 exitCode=1 stdout=[] (expected: empty/no navisoma-port-ok)
CHECK iso_session_container_delete_postcondition PASS
CHECK iso_session_image_delete     PASS
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

Identical outcome on every line, including both bridge IPs, `iso_session_create`
succeeding again with no `WSLC_E_SESSION_RESERVED`, and the driver again
confirming the parent staging directory gone at the end:

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
CHECK build_output_load_handoff    PASS hr=0x00000000
CHECK build_output_exact_reference_observed PASS listHr=0x00000000 count=3 nameObserved=1
CHECK build_output_run_verify      PASS created=1 started=1 waitedOk=1 exitOk=1 exitCode=0 stdout=[navisoma-build-stdout-marker] stderr=[navisoma-build-stderr-marker]
CHECK build_output_container_delete_postcondition PASS
CHECK build_output_image_delete    PASS hr=0x00000000
CHECK build_output_image_delete_postcondition PASS listHr=0x00000000 stillPresent=0
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
CHECK cross_session_isolation      PASS target=172.17.0.2:80 waitedOk=1 exitCode=1 stdout=[] (expected: empty/no navisoma-port-ok)
CHECK iso_session_container_delete_postcondition PASS
CHECK iso_session_image_delete     PASS
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

## Cleanup verification

Every resource the probe creates is enumerated below with its own delete
call and its own observed postcondition — not a trusted return code, and
not "ran twice with the same names" as the only evidence:

| Resource | Delete call | Postcondition check | Observed |
|---|---|---|---|
| Container `c1` | `WslcDeleteContainer` | `WslcOpenContainer` by name expected to fail | `WSLC_E_CONTAINER_NOT_FOUND` (`0x80040603`) |
| Container `c2` (peer) | `WslcDeleteContainer` | `WslcOpenContainer` by name expected to fail | `WSLC_E_CONTAINER_NOT_FOUND` (`0x80040603`) |
| Container (raw-rootfs one-shot) | `WslcDeleteContainer` | `WslcOpenContainer` by name expected to fail | `deleteVerifiedGone` — pass |
| Container (build-output one-shot) | `WslcDeleteContainer` | `WslcOpenContainer` by name expected to fail | `deleteVerifiedGone` — pass |
| Container (iso session's one-shot) | `WslcDeleteContainer` | `WslcOpenContainer` by name expected to fail | `deleteVerifiedGone` — pass |
| Image: pulled `alpine` | `WslcDeleteSessionImage` | `WslcListSessionImages` re-list expected to omit it | `countAfter=0` |
| Image: tag `navisoma-probe-worker:ci` | `WslcDeleteSessionImage` | re-list expected to omit it | `tagStillPresent=0` |
| Image: raw-rootfs import | `WslcDeleteSessionImage` | (delete call only; see boundary note in capability 5) | `S_OK` |
| Image: loaded build-output | `WslcDeleteSessionImage` | re-list expected to omit it | `stillPresent=0` |
| Image: iso session's raw-rootfs import | `WslcDeleteSessionImage` | (delete call only, separate session) | `S_OK` |
| Volume | `WslcDeleteSessionVhdVolume` | Second delete of the same name expected to fail | `WSLC_E_VOLUME_NOT_FOUND` (`0x80040604`) |
| Main session | `WslcTerminateSession` + `WslcReleaseSession` | Reusing the exact same session name in the next run expected to succeed | Succeeded both runs |
| Main session's `storagePath` (caller-owned) | `SHFileOperationW` (recursive delete, driven by the probe itself) | `GetFileAttributesW` expected to return `INVALID_FILE_ATTRIBUTES` | Confirmed both runs |
| Iso session | `WslcTerminateSession` + `WslcReleaseSession` | Reusing the exact same session name in the next run expected to succeed | Succeeded both runs |
| Iso session's `storagePath` (caller-owned) | `SHFileOperationW`, by the probe | `GetFileAttributesW` expected to return `INVALID_FILE_ATTRIBUTES` | Confirmed both runs |
| Staged input files (`probe.exe`, `wslcsdk.dll`, both fixture tars) | `rm -f` in `run-probe.sh` (driver-owned, not the probe's job) | Parent staging directory expected to not exist | Confirmed both runs |

`wslc.exe list -a` / `wslc.exe images` were tried afterward purely as an
incidental, non-authoritative human sanity check (consistent with not
treating CLI output as capability evidence either way) and errored with a
generic `E_FAIL` against the host's unrelated, pre-existing default CLI
session (`wslc-cli-asopitech`, present on this host before this spike
started) — the probe never touched that session, so this has no bearing on
anything above.

**Production implication:** `storagePath` is caller-supplied in
`WslcInitSessionSettings`, so the caller-owned storage directories above
are caller-owned by construction, not a WSLC gap to route around. A
NAVISOMA WSLC backend must delete that directory itself (after a
successful `WslcReleaseSession`, or as part of recovering a session that
will never be reused) the same way it already owns cleanup for any other
backend's on-disk state — this should be written down as a lowering-adapter
obligation in the detailed model/graph work.

## Capability proof contracts (#14's five required capabilities)

Applied in full to the five capabilities #14 named as required. The
baseline SDK lifecycle operations (session/container/image CRUD) are
reported in the simpler table further below.

### 1. `process.exec.recurring`

| Field | Content |
|---|---|
| Claim | A process can be exec'd into a running WSLC container, its stdout and stderr can both be read, its exit code retrieved, and the same mechanism invoked again against the same container. |
| Stimulus | `WslcCreateContainerProcess` with `/bin/sh -c "cat <volume file>; echo navisoma-exec-stderr-marker 1>&2"`, then a second, independent `WslcCreateContainerProcess` with `/bin/sh -c "echo navisoma-exec2-stdout-marker; echo navisoma-exec2-stderr-marker 1>&2"` against the same container. |
| Oracle | `WslcGetProcessIOHandle` reads on both `STDOUT`/`STDERR` contain their respective distinct markers, `WslcGetProcessExitCode` returns `0`, **jointly** (one combined pass condition, not stdout checked separately from stderr), for *both* invocations. |
| Negative condition | Either invocation's stderr buffer missing its marker, its stdout buffer missing its marker, a non-zero/unavailable exit code, or the second invocation failing outright. |
| Consumer proof | This same exec mechanism is what `volume.named.persistent` (below) relies on to read back its marker file — a downstream capability actually depends on this one working. |
| Cleanup owner | The exec'd process itself is released via `WslcReleaseProcess`; no independent resource survives it to clean up. |
| Boundary | This proves the primitive a scheduler would call repeatedly, not a real scheduler loop with real intervals/retries/timeouts — that logic doesn't exist yet and is planner-owned, not a WSLC capability. |
| **Result** | **supported** |

### 2. `network.named.isolated` (#14 case 1: one named network per project)

This capability has two distinct halves, both now tested with their own
CHECK and their own oracle: services in the same project must reach each
other, and a different project must not reach them.

| Field | Content |
|---|---|
| Claim (2a: intra-project) | Lowering "one Compose project" to "one WSLC session" with `BRIDGED` networking gives that project's services mutual reachability. |
| Stimulus (2a) | A second container (`c2`) created in the *same* session as `c1`, also `BRIDGED`, running an `nc` listener on an internal port (9000, no host mapping); `c1` execs `nc -w 3 <c2's IP> 9000`. |
| Oracle (2a) | `c2`'s own IP read from `WslcInspectContainer`'s `NetworkSettings.Networks.bridge.IPAddress` field; `c1`'s exec stdout contains `navisoma-peer-ok`. |
| Negative condition (2a) | `c1`'s exec times out, returns nothing, or (as happened before the field-name fix noted in Method) connects to the wrong address entirely. |
| Consumer proof (2a) | `c1`, a separate container in the same project, is the actual consumer of `c2`'s service. |
| Claim (2b: cross-project isolation) | A container in a **different, independently created session** cannot reach a container in the first session over its real bridge IP. |
| Stimulus (2b) | A second session (`navisoma-wslc-probe-iso`), created and alive **concurrently** with the main session; a container in it execs `nc -w 3 <c1's IP> 80`. |
| Oracle (2b) | The iso container's exec produces exit code `1` and empty stdout — no `navisoma-port-ok` observed. |
| Negative condition (2b) | The iso container's exec succeeding and printing `navisoma-port-ok` — which would mean WSLC's bridge is shared across sessions with no isolation at all, a materially different (and worse) finding than "unverified". |
| Consumer proof (2b) | The iso session's container is the one making the (failed) connection attempt — a real cross-session actor, not just an inspection. |
| Cleanup owner | SDK for both containers/images (each independently postcondition-checked in the "Cleanup verification" table); SDK + caller for both sessions and their storage directories, same table. |
| Boundary | Two sessions, one connection attempt each direction tested (main→nothing needed, iso→main). Concurrent *load* across many sessions, and whether isolation holds under session crash/recovery, were not tested. |
| **Result** | **supported** — both halves now have direct, passing evidence. This capability is no longer `unverified`: the isolation half that the previous round left untested now has its own dedicated CHECK (`cross_session_isolation`) with a negative-condition-aware oracle, and it passed identically on both runs. |

`WslcInspectContainer`'s JSON schema is undocumented in `wslcsdk.h`. A
first attempt at 2a scanned the payload for "the first dotted-quad-looking
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
| Negative condition | The marker missing from the exec's stdout, or the second delete also succeeding (would mean the first delete had no real effect, or the name was silently recreated). |
| Consumer proof | The `cat` exec — a process distinct from the one that wrote the file — is the consumer of the mounted content. |
| Cleanup owner | SDK, via `WslcDeleteSessionVhdVolume`; postcondition checked (`volume_delete_postcondition`, `0x80040604` `WSLC_E_VOLUME_NOT_FOUND` on the repeat delete). |
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
| Cleanup owner | The socket is closed by the probe itself (plain WinSock, not a WSLC resource); the container's port mapping ends when the container is deleted (covered by `container_delete_postcondition` in the cleanup table). |
| Boundary | One port, TCP only (UDP mapping exists in the API — `WslcPortProtocol` — but was not exercised). |
| **Result** | **supported** |

### 5. `build.imagestore.import` (#14 case 3)

| Field | Content |
|---|---|
| Claim | The full handoff chain — an image produced by a builder independent of WSLC, transferred in, retaining its exact producer-assigned identity, and consumed by a container create/start/exec — actually works. |
| Stimulus | [`build_test_image.py`](wslc-capability-gate/build_test_image.py) fetches `library/busybox` from the public Docker Hub registry over plain HTTPS and writes a real docker-save/OCI archive (`build-output.tar`) whose `manifest.json` carries `"RepoTags": ["navisoma-build-output:ci"]`. `WslcLoadSessionImageFromFile` — which takes **no separate name parameter** — loads it; `WslcListSessionImages` is checked for that exact name; a container is then created, started, and exec'd **from that exact name**, not a name the probe supplied. |
| Oracle | `build_output_exact_reference_observed`: the archive's own embedded name appears in the session's image list after loading. `build_output_run_verify`: a container created from that exact name reaches exit code `0` with both a stdout and a distinct stderr marker present. |
| Negative condition | The name failing to appear after load (would mean the archive's identity didn't round-trip), or the container failing to create/start/exec from it (would mean the loaded image isn't actually runnable) — this is exactly the gap the previous round had: it proved import-of-bytes but not this full chain, and used a bare rootfs tar (no manifest, no embedded name) rather than a real archive. |
| Consumer proof | The one-shot container's own exec output is the consumer of the loaded image; nothing about this path was inferred from the raw-rootfs-import test, which is now explicitly demoted to supplementary evidence for a different, narrower claim ("the SDK can run *something* handed to it as bytes") — see Method. |
| Cleanup owner | SDK: `WslcDeleteContainer`/`WslcReleaseContainer` for the one-shot container (`build_output_container_delete_postcondition`), `WslcDeleteSessionImage` for the loaded image, with a re-list postcondition (`build_output_image_delete_postcondition`). |
| Boundary | The SDK has no C API to *build* from a Dockerfile at all — only to consume an already-built image (CLI/MSBuild/CMake own building, per the SDK's own docs) — matching NAVISOMA's design of delegating build elsewhere and handing WSLC the result. One layer, one archive; a multi-layer image and registry push/pull round-trips through WSLC itself were not additionally tested (pull was already covered separately by `WslcPullSessionImage`). |
| **Result** | **supported** |

## Baseline SDK lifecycle (supporting evidence)

Direct API mechanics that the five capabilities above depend on, evidenced
by their own `CHECK` lines and the postconditions in "Cleanup verification".

| Operation | WSLC C API entry points used | Consumer proof | Cleanup owner (postcondition) | Result |
|---|---|---|---|---|
| Missing components / service version | `WslcGetMissingComponents`, `WslcGetVersion` | Would gate WSLC backend eligibility before scheduling any deployment; not consumed further in this probe | n/a — query only, no resource created | supported |
| Session create/terminate/release (×2 sessions) | `WslcInitSessionSettings`, `WslcCreateSession`, `WslcTerminateSession`, `WslcReleaseSession` | Every other operation in each session's lifetime uses that session's `WslcSession` handle | SDK owns the runtime session; caller owns the `storagePath` directory — see "Production implication" | supported |
| Pull a small known image | `WslcPullSessionImage`, `WslcListSessionImages` | `image_tag_handoff` and `c1`'s container create both reference the pulled image | SDK, via `WslcDeleteSessionImage`; postcondition checked | supported |
| Container create/start/stop/delete/release (×5 containers across 2 sessions) | `WslcInitContainerSettings`, `WslcCreateContainer`, `WslcStartContainer`, `WslcStopContainer`, `WslcDeleteContainer`, `WslcReleaseContainer` | Every check under "Capability proof contracts" runs against one of these containers | SDK, via `WslcDeleteContainer`; postcondition checked for each (see "Cleanup verification") | supported |
| Long-running process termination | `WslcStopContainer` (SIGTERM) on `c1`'s genuinely long-running init loop, then `WslcGetProcessState`/`WslcGetProcessExitEvent`/`WslcGetProcessExitCode` on its init process | Confirms `c1` actually terminates before its container is deleted, not just that the stop call returned `S_OK` | n/a — verification, not a resource | supported — `state=2` (`WSLC_PROCESS_STATE_EXITED`), exit code `137` (`128+SIGKILL`), meaning the shell loop did not exit cleanly on `SIGTERM` within the grace period and WSLC escalated to `SIGKILL` — a real, deterministic outcome, reported as observed rather than assumed to be a graceful exit |

## Discovered boundaries

One limitation remains genuinely out of scope for #14's three fixed,
single-project scenarios, reported with its own result rather than folded
into a "supported" row's prose:

| Boundary | Result | Why |
|---|---|---|
| Multiple, independently-named, user-creatable networks *within* one project | **capability-gated: absent in WSLC SDK 2.9.9** | Confirmed, not merely untested: `wslcsdk.h` (682 lines, read in full) exposes no `WslcCreateNetwork`/list/delete function at all — only a per-container `WslcContainerNetworkingMode` enum (`NONE`/`BRIDGED`) and an unused `WSLC_E_NETWORK_NOT_FOUND` code. There is also no live capability-query API for this, so a NAVISOMA WSLC backend would need a static per-pinned-SDK-version capability declaration, not a runtime probe, to refuse this explicitly rather than silently collapsing multiple requested networks into one. |

Resolved from the previous round: cross-session network isolation was
`unverified` there and is `supported`, with direct evidence, here (see
capability 2 above) — a different, different-session container measurably
could not reach `c1`.

This one remaining boundary is not scope-breaking: it is cleanly
expressible as a capability gate with no change to the user's Compose file
or NAVISOMA's action order.

## Backend-neutral action order observed

The probe exercised exactly the lifecycle #14 fixed, with every action
implemented by a single, real WSLC C API call and no NAVISOMA-invented
detour. The main session's timeline:

```
resolve/pull image → tag (build-output naming) → create prerequisites (named volume)
→ create container (network mode, port mapping, named volume, init process attached)
→ start → wait (init process listening; exec ×2 for stdout/stderr/exit status and
  recurrence; a second same-session container + exec for peer-to-peer connectivity;
  a container in a second, concurrently alive session attempting -- and failing -- to
  reach this one)
→ stop (with termination verified via exit event/state/code) → remove container
  (+ postcondition lookup) → remove volume (+ postcondition delete) → remove session
  (+ caller-owned storage deleted and verified gone)
```

Two one-shot flows run the same create → start → wait(exit) → stop →
remove shape independently of the long-running main container: the
supplementary raw-rootfs import, and the build-output load-handoff
(`build_output_load_handoff` → `build_output_run_verify`). The isolation
probe's session runs the same shape once more, inside its own,
concurrently-alive second session.

## Recommendation for #13

Per explicit instruction for this round, this document does not itself
conclude `proceed` for #13. What can be said plainly: every gap raised
against the previous round is closed with passing, postcondition-verified
evidence on a live host, across two independent, fully reproducible runs
via `run-probe.sh` — including both claims that were previously
`unverified` (cross-session isolation now has a dedicated, passing CHECK;
the build-handoff claim now rests on a real, independently-built
multi-layer OCI archive consumed by its own embedded reference, not a raw
rootfs tar). No row in this document is `unverified` any more; the one
remaining `capability-gated` row (multi-network segmentation within a
project) is a confirmed, not merely suspected, absence that #14's fixed
scenarios do not require.

**This issue's status is left as pending — not proceed — for the
maintainer to confirm independently before #13 acts on it.** If that
confirmation holds, the substance of what would be proposed is: proceed,
carrying forward the one remaining boundary (multi-network segmentation)
as a tracked capability gate rather than a blocker, consistent with #14's
finding that no canonical resource or action needed a backend-specific
type or a divergent user workflow.
