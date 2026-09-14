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

## Policy compliance

[`docs/validation/work-instruction-policy.md`](work-instruction-policy.md)
was added to this repository while this issue was in review and now
governs validation work under `docs/validation/`. This document (and the
probe it describes) were revised to comply with it after the fact,
per that policy's own rule that review findings replace a conclusion
rather than append a caveat to it: the "Capability proof contracts"
section below states, for each of #14's five required capabilities, the
claim, stimulus, oracle, negative condition, consumer proof, cleanup
owner/postcondition, and boundary, and `unverified`/`capability-gated` are
used explicitly for the two things this spike did not or cannot confirm,
rather than folding them into prose under a bare `supported`.

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
CHECK process_exec                 PASS
CHECK process_stdio_exit_status    PASS waitedOk=1 exitOk=1 exitCode=0 stdout=[navisoma-volume-marker] stderr=[navisoma-exec-stderr-marker]
CHECK named_volume_mount           PASS marker read back through exec
CHECK process_exec_second_invocation PASS execOk=1 waitedOk=1 exitOk=1 exitCode=0 stdout=[navisoma-exec2-stdout-marker] stderr=[navisoma-exec2-stderr-marker]
CHECK peer_container_ip_inspect    PASS 172.17.0.3
CHECK intra_session_service_connectivity PASS peerIp=172.17.0.3 waitRes=0 stdout=[navisoma-peer-ok]
CHECK container_stop               PASS hr=0x00000000
CHECK container_delete             PASS hr=0x00000000
CHECK container_release            PASS
CHECK container_delete_postcondition PASS reopen hr=0x80040603 (expected FAILED = truly deleted)
CHECK volume_delete                PASS hr=0x00000000
CHECK volume_delete_postcondition  PASS second delete hr=0x80040604 (expected FAILED = truly deleted)
CHECK image_tag_delete             PASS hr=0x00000000
CHECK image_pull_delete            PASS hr=0x00000000
CHECK image_pull_delete_postcondition PASS listHr=0x00000000 countAfter=0 alpineStillPresent=0
CHECK session_terminate            PASS
CHECK session_release              PASS
PROBE_DONE failures=0
```

### Run 2 (immediately after run 1, no manual cleanup)

Identical outcome on every line, including `session_create PASS` reusing
the exact same session name, the peer container getting the exact same
bridge IP again, and `WSLC_E_CONTAINER_NOT_FOUND` (`0x80040603`) /
`WSLC_E_VOLUME_NOT_FOUND` (`0x80040604`) — the SDK's own documented codes —
coming back on the post-delete lookups both times:

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
CHECK process_exec                 PASS
CHECK process_stdio_exit_status    PASS waitedOk=1 exitOk=1 exitCode=0 stdout=[navisoma-volume-marker] stderr=[navisoma-exec-stderr-marker]
CHECK named_volume_mount           PASS marker read back through exec
CHECK process_exec_second_invocation PASS execOk=1 waitedOk=1 exitOk=1 exitCode=0 stdout=[navisoma-exec2-stdout-marker] stderr=[navisoma-exec2-stderr-marker]
CHECK peer_container_ip_inspect    PASS 172.17.0.3
CHECK intra_session_service_connectivity PASS peerIp=172.17.0.3 waitRes=0 stdout=[navisoma-peer-ok]
CHECK container_stop               PASS hr=0x00000000
CHECK container_delete             PASS hr=0x00000000
CHECK container_release            PASS
CHECK container_delete_postcondition PASS reopen hr=0x80040603 (expected FAILED = truly deleted)
CHECK volume_delete                PASS hr=0x00000000
CHECK volume_delete_postcondition  PASS second delete hr=0x80040604 (expected FAILED = truly deleted)
CHECK image_tag_delete             PASS hr=0x00000000
CHECK image_pull_delete            PASS hr=0x00000000
CHECK image_pull_delete_postcondition PASS listHr=0x00000000 countAfter=0 alpineStillPresent=0
CHECK session_terminate            PASS
CHECK session_release              PASS
PROBE_DONE failures=0
```

### Cleanup verification

Per the policy's cleanup rule, SDK-owned and caller-owned cleanup are
reported and evidenced separately, not conflated into one "it cleaned up"
claim:

- **SDK-owned runtime state** (container, volume, image, session): each
  deletion's *own* postcondition is checked, not just its return code —
  `container_delete_postcondition` re-opens the container by name and
  requires that to fail (`WSLC_E_CONTAINER_NOT_FOUND`, `0x80040603`),
  `volume_delete_postcondition` deletes the same volume name again and
  requires that second delete to fail (`WSLC_E_VOLUME_NOT_FOUND`,
  `0x80040604`), and `image_pull_delete_postcondition` re-lists the
  session's images and requires the deleted name to actually be absent
  (`countAfter=0`). All three passed on both runs. Run 2 also reusing every
  fixed name without `WSLC_E_SESSION_RESERVED` is corroborating evidence at
  the session level, but the postcondition checks are the primary evidence
  now, not name-reuse alone.
- **Caller-owned state** (the `storagePath` directory given to
  `WslcInitSessionSettings`, and the probe's own staged files): the SDK
  does not delete this on `WslcTerminateSession`/`WslcReleaseSession` —
  observed directly, the directory was still present after both runs —
  and it was removed manually as part of closing out the spike. See
  "Production implication" below.

`wslc.exe list -a` / `wslc.exe images` were tried afterward purely as an
incidental, non-authoritative human sanity check (consistent with not
treating CLI output as capability evidence either way) and errored with a
generic `E_FAIL` against the host's unrelated, pre-existing default CLI
session (`wslc-cli-asopitech`, present on this host before this spike
started) — a CLI-only observation this gate does not rely on and that has
no bearing on the capability proof contracts below, since the probe never
touched that session.

**Production implication:** cleanup here is not fully automatic, and this
is not a WSLC defect to work around — `storagePath` is caller-supplied in
`WslcInitSessionSettings`, so it is caller-owned storage by construction.
A NAVISOMA WSLC backend must treat deleting that directory (after a
successful `WslcReleaseSession`, or as part of recovering from a session
that will never be reused) as its own explicit responsibility, the same
way it already owns cleanup for any other backend's on-disk state. This
should be written down as a lowering-adapter obligation for the WSLC
backend in the detailed model/graph work, not left implicit.

## Capability proof contracts (#14's five required capabilities)

Applied in full to the five capabilities #14 named as required and
therefore under the most scrutiny. The baseline SDK lifecycle operations
(session/container/image CRUD) are reported in the simpler table further
below — they are supporting infrastructure for these five, evidenced
directly by the raw `CHECK` output and the postconditions above, not
separate product-relevant capability claims in their own right.

### 1. `process.exec.recurring`

| Field | Content |
|---|---|
| Claim | A process can be exec'd into a running WSLC container, its stdout and stderr can both be read, its exit code retrieved, and the same mechanism invoked again against the same container. |
| Stimulus | `WslcCreateContainerProcess` with `/bin/sh -c "cat <volume file>; echo navisoma-exec-stderr-marker 1>&2"`, then a second, independent `WslcCreateContainerProcess` with `/bin/sh -c "echo navisoma-exec2-stdout-marker; echo navisoma-exec2-stderr-marker 1>&2"` against the same container. |
| Oracle | `WslcGetProcessIOHandle` reads on both `STDOUT`/`STDERR` contain their respective distinct markers, `WslcGetProcessExitCode` returns `0`, for *both* invocations. |
| Negative condition | Either invocation's stderr buffer missing its marker (this is exactly what the first version of this probe would have missed, since it never emitted stderr at all), a non-zero/unavailable exit code, or the second invocation failing outright. |
| Consumer proof | `named_volume_mount` (below) consumes the first exec's stdout content as its own evidence; the second exec's sole purpose is proving repeatability. |
| Cleanup owner | The exec'd process itself is released via `WslcReleaseProcess`; no independent resource survives it to clean up. |
| Boundary | This proves the primitive a scheduler would call repeatedly, not a real scheduler loop with real intervals/retries/timeouts — that logic doesn't exist yet and is planner-owned, not a WSLC capability. |
| **Result** | **supported** |

### 2. `network.named.isolated` (#14 case 1: one named network per project)

| Field | Content |
|---|---|
| Claim | Lowering "one Compose project" to "one WSLC session" with `BRIDGED` networking gives that project's services mutual reachability, with no WSLC concept of a separately named/created network object. |
| Stimulus | A second container (`c2`) created in the *same* session as `c1`, also `BRIDGED`, running a `nc` listener on an internal port (9000, no host mapping); `c1` execs `nc -w 3 <c2's IP> 9000`. |
| Oracle | `c2`'s own IP is read from `WslcInspectContainer`'s `NetworkSettings.Networks.bridge.IPAddress` field (`172.17.0.3` both runs); `c1`'s exec stdout contains `navisoma-peer-ok`. |
| Negative condition | `c1`'s exec times out, returns nothing, or connects to the wrong address — this is exactly what happened before the field-name fix described below the table. |
| Consumer proof | `c1`, a separate container in the same project, is the actual consumer of `c2`'s service — not just an inspection of `c2` in isolation. |
| Cleanup owner | SDK: `c2` is stopped/deleted/released the same way `c1` is (not separately re-verified with its own postcondition check, since the mechanism is identical to `c1`'s, which is checked). |
| Boundary | Proves intra-project connectivity + (separately, `port.publish.tcp` below) host reachability. Does **not** prove isolation *from* a different project/session — see "Discovered boundaries" below. |
| **Result** | **supported** (for the intra-project connectivity #14 case 1 actually needs) |

`WslcInspectContainer`'s JSON schema is undocumented in `wslcsdk.h`. A
first attempt scanned the payload for "the first dotted-quad-looking
substring" and got `172.17.0.1` — the bridge's *gateway*, not `c2`'s own
address (`172.17.0.3`) — because `"Gateway"` sorts before `"IPAddress"` in
the emitted JSON. This was caught by printing the real payload rather than
trusting the heuristic, and the probe now looks up the named `IPAddress`
field specifically.

### 3. `volume.named.persistent` (#14 case 1)

| Field | Content |
|---|---|
| Claim | A named volume created independently of any container can be mounted into a container, retains content written to it, and can be deleted independently once no container needs it. |
| Stimulus | `WslcCreateSessionVhdVolume` before any container exists; `WslcSetContainerSettingsNamedVolumes` attaches it to `c1` at `/mnt/probe-vol`; `c1`'s init process writes a marker file into it; a separate exec (`cat`) reads it back; `WslcDeleteSessionVhdVolume` after `c1` is already deleted. |
| Oracle | The exec's stdout contains `navisoma-volume-marker`; the delete call returns `S_OK`; a second delete of the same name then fails. |
| Negative condition | The marker missing from the exec's stdout (mount didn't work, or didn't persist between the writer and the reader process), or the second delete also succeeding (would mean the first delete had no real effect, or the name was silently recreated). |
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
| Cleanup owner | The socket is closed by the probe itself (plain WinSock, not a WSLC resource); the container's port mapping ends when the container is deleted (covered by `container_delete_postcondition`). |
| Boundary | One port, TCP only (UDP mapping exists in the API — `WslcPortProtocol` — but was not exercised). |
| **Result** | **supported** |

### 5. `build.imagestore.import` (#14 case 3)

| Field | Content |
|---|---|
| Claim | An image produced outside WSLC (by tagging an existing pulled reference, and separately by importing raw external bytes) can be consumed by a WSLC container create/start, not merely accepted by an SDK call and discarded. |
| Stimulus | Path A: `WslcTagSessionImage` retags the pulled `alpine` image as `navisoma-probe-worker:ci`; `c1` is created and started **from that tag**. Path B: `WslcImportSessionImageFromFile` imports a fixture tar (a real, independently-sourced static `busybox` binary, not produced by any container runtime) as `navisoma-probe-import:test`; a one-shot container is then created, started, and exec'd **from that imported reference**. |
| Oracle | Path A: `c1`'s own checks (port/volume/exec, all above) all pass, proving the tag is genuinely runnable, not just accepted. Path B: `image_import_run_verify`'s container reaches exit code `0` with both a stdout and a distinct stderr marker present. |
| Negative condition | Path A: `container_create`/`container_start` failing against the tag. Path B: the one-shot container failing to create/start, or its markers/exit code being wrong — which is exactly the earlier gap (the first version of this probe imported and immediately deleted the image, proving only that the SDK accepts external bytes, not that WSLC can run a container from them). |
| Consumer proof | The one-shot container's exec output for path B; the entire rest of `c1`'s test surface for path A. |
| Cleanup owner | SDK: `WslcDeleteContainer`/`WslcReleaseContainer` for the one-shot container (path B), `WslcDeleteSessionImage` for both the tag and the import (`image_tag_delete`, and the delete inside path B's own block). |
| Boundary | The SDK has no C API to *build* from a Dockerfile at all — only to consume an already-built image (CLI/MSBuild/CMake own building, per the SDK's own docs) — matching NAVISOMA's design of delegating build elsewhere and handing WSLC the result. The import fixture is a single-layer flat rootfs (`WslcImportSessionImageFromFile`'s "docker import"-shaped semantics); a full multi-layer OCI/docker-save archive via `WslcLoadSessionImageFromFile` was not tested. |
| **Result** | **supported** |

## Baseline SDK lifecycle (supporting evidence)

Direct API mechanics that the five capabilities above depend on, evidenced
by their own `CHECK` lines and the postconditions described under
"Cleanup verification". Listed for completeness rather than given a full
per-field contract, since they are lower-scrutiny SDK plumbing rather than
product-relevant capability claims in their own right.

| Operation | WSLC C API entry points used | Consumer proof | Cleanup owner (postcondition) | Result |
|---|---|---|---|---|
| Missing components / service version | `WslcGetMissingComponents`, `WslcGetVersion` | Would gate WSLC backend eligibility before scheduling any deployment; not consumed further in this probe | n/a — query only, no resource created | supported |
| Session create/terminate/release | `WslcInitSessionSettings`, `WslcCreateSession`, `WslcTerminateSession`, `WslcReleaseSession` | Every other operation in the run uses the returned `WslcSession` handle | SDK owns the runtime session (`session_terminate`/`session_release`); caller owns the `storagePath` directory — see "Production implication" | supported |
| Pull a small known image | `WslcPullSessionImage`, `WslcListSessionImages` | `image_tag_handoff` and `c1`'s container create both reference the pulled image | SDK, via `WslcDeleteSessionImage`; postcondition checked (`image_pull_delete_postcondition`, re-list shows the name absent) | supported |
| Container create/start/stop/delete/release | `WslcInitContainerSettings`, `WslcCreateContainer`, `WslcStartContainer`, `WslcStopContainer`, `WslcDeleteContainer`, `WslcReleaseContainer` | Every check under "Capability proof contracts" runs against this container | SDK, via `WslcDeleteContainer`; postcondition checked (`container_delete_postcondition`, re-open fails with `WSLC_E_CONTAINER_NOT_FOUND`) | supported |

## Discovered boundaries (not required by #14, reported for completeness)

Two limitations surfaced during this spike that #14's three fixed,
single-project scenarios do not actually require evidence for. Per the
policy, they are named explicitly with their own result rather than folded
into a "supported" row's prose:

| Boundary | Result | Why |
|---|---|---|
| Multiple, independently-named, user-creatable networks *within* one project | **capability-gated: absent in WSLC SDK 2.9.9** | Confirmed, not merely untested: `wslcsdk.h` (682 lines, read in full) exposes no `WslcCreateNetwork`/list/delete function at all — only a per-container `WslcContainerNetworkingMode` enum (`NONE`/`BRIDGED`) and an unused `WSLC_E_NETWORK_NOT_FOUND` code. There is also no live capability-query API for this, so a NAVISOMA WSLC backend would need a static per-pinned-SDK-version capability declaration, not a runtime probe, to refuse this explicitly rather than silently collapsing multiple requested networks into one. |
| Isolation *between* two different sessions/projects | **unverified** | Not a confirmed absence — genuinely not tested. Every container this spike created lived in one session; nothing was created in a second session to check whether it could observe or reach the first session's containers. It's an architecturally-plausible consequence of each session getting its own VM-like boundary (each has its own storage VHDX and can have its own CPU/memory settings), but that plausibility is not the same as the direct evidence this spike produced for the five required capabilities above, and is not asserted as such. |

Neither boundary is scope-breaking: both are cleanly expressible as a
capability gate (or, for the second, as a specific follow-up probe) with
no change to the user's Compose file or NAVISOMA's action order.

## Backend-neutral action order observed

The probe exercised exactly the lifecycle #14 fixed, with every action
implemented by a single, real WSLC C API call and no NAVISOMA-invented
detour:

```
resolve/pull image → tag (build-output naming) → create prerequisites (named volume)
→ create container (network mode, port mapping, named volume, init process attached)
→ start → wait (init process listening; exec ×2 for stdout/stderr/exit status and
  recurrence; a second same-session container + exec for peer-to-peer connectivity)
→ stop → remove container (+ postcondition lookup) → remove volume (+ postcondition
  delete) → remove session
```

The import-handoff path (`image_import_handoff` / `image_import_run_verify`)
runs the same create → start → wait(exit) → stop → remove shape as a
one-shot container, independently of the long-running main container above.

## Recommendation for #13

This issue's own result is **proceed to the next validation**: every
capability #14 flagged as required is now backed by a full proof contract
(claim, stimulus, oracle, negative condition, consumer proof, cleanup
owner/postcondition, boundary) with evidence from two independent,
identical, fully-postcondition-verified runs — not name-reuse alone. This
supersedes the earlier version of this evidence, which a static review
correctly found insufficient on three points (stdout-only exec assertions,
import-without-consumption, and an unverified network-isolation claim
resting on the published-port test alone); those gaps are closed above,
along with strengthening every cleanup claim from "the delete call
returned `S_OK`" to an actual observed postcondition.

**Proposal for #13: proceed** — conditional on the two discovered,
out-of-scope boundaries above being carried forward as tracked follow-ups
rather than silently dropped: multi-network segmentation within one
project (`capability-gated`, confirmed absent) and inter-session isolation
(`unverified`, genuinely untested). Neither blocks #14's three fixed
scenarios, all of which are single-project. Combined with #14's finding
that no canonical resource or action needed a backend-specific type or a
divergent user workflow, both child gates now support #13 moving to the
detailed model/graph work in #3.
