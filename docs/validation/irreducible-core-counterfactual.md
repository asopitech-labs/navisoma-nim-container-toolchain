# Irreducible core counterfactual — Issue #16

## Conclusion

No end-to-end health gate was found in the three selected tool families.
However, that does not establish NAVISOMA-specificity. Therefore, no
NAVISOMA-specific core has been established, and this does not justify
production implementation.

## Fixed candidate

```yaml
services:
  api:
    depends_on:
      db:
        condition: service_healthy
```

Question: can Docker Compose, the containerd/nerdctl family, or the WSLC
SDK/`wslc` family — used as-is, or merely composed together — execute this
health-gated dependency graph with the same action order, derived health
state, and failure contract across both containerd and WSLC? This
document investigates only these three alternatives, from primary
documentation and API surfaces, with no probe, host run, FFI, or further
WSLC capability validation.

## Findings table

| Alternative | Reads Compose health gate | containerd | WSLC | Same failure contract | Exact missing handoff |
|---|---|---|---|---|---|
| **Docker Compose** | Yes — `condition: service_healthy` is defined in the Compose Specification itself: *"service_healthy: Specifies that a dependency is expected to be 'healthy' ... before starting a dependent service"* and *"Compose guarantees dependency services marked with `service_healthy` are 'healthy' before starting a dependent service."* [[compose-spec/compose-spec, 05-services.md]](https://github.com/compose-spec/compose-spec/blob/main/05-services.md) | Indirect only. `docker/compose`'s backend (`pkg/compose`) calls the Docker Engine API via the Moby client library, not containerd's own client/gRPC API. [[docker/compose, pkg/compose/compose.go]](https://github.com/docker/compose/blob/main/pkg/compose/compose.go) [[docker/compose, pkg/api/api.go]](https://github.com/docker/compose/blob/main/pkg/api/api.go) | No. No Docker Compose backend targets WSLC; WSLC is a separate product surface with no Docker Engine API endpoint. | N/A — never reaches a second backend to compare against. | Docker Compose implements the health-gate contract, but only against the Docker Engine API, which is a different, proprietary surface from the containerd client API #14 fixed as this project's containerd target, and has no WSLC path at all. |
| **containerd / nerdctl family** | Not established with a primary/authoritative source in this investigation. nerdctl compose accepts a Compose file and is built on the containerd client API, which itself has no health-check or dependency-gating concept ([containerd.io docs, service list: `images`, `tasks`, `content`, `snapshots`, `namespaces`, `diff`, `version`, `events`, `leases`, `introspection`](https://containerd.io/docs/2.2/getting-started/)); whether nerdctl's own compose layer additionally implements or drops the `service_healthy` semantics was not confirmed here to a primary-source standard, and no further search for one was made. | Yes (nerdctl is containerd-native, by its own stated design). | No. nerdctl targets containerd exclusively; no WSLC backend exists. | Not established here, for the reason above. | Not established here beyond: containerd's own client API has no health-gating primitive, so any such behavior in nerdctl (if present) would be nerdctl-owned, not containerd-owned — and nerdctl has no WSLC target in any case. |
| **WSLC SDK / `wslc` family** | No. The pinned `wslcsdk.h` (Microsoft.WSL.Containers 2.9.9, SHA-256 `e2867c46e51db62b7307dc858c2f5cd767d298760b272ad19a161145bd1bbba1`) was read in full (682 lines) for #14/#15 and declares no Compose-file parsing, no `healthcheck` type, and no multi-container dependency primitive of any kind — only single-container lifecycle (`WslcCreateContainer`/`WslcStartContainer`/.../`WslcDeleteContainer`) plus a generic `WslcCreateContainerProcess` exec primitive; #14's own Case 2 analysis states plainly that *"no health concept exists inside the backend itself."* [[this repo, `semantic-core-gate.md`, Case 2 table]](semantic-core-gate.md) [[this repo, `wslc-capability-gate.md`, "682 lines, read in full"]](wslc-capability-gate.md) | No. WSLC is a separate Windows-native session/VM-based container runtime, unrelated to containerd (confirmed directly: #15's probe never touched containerd in any form). | Yes, trivially (it is the WSLC family). | N/A — no cross-backend targeting exists for this family to begin with. | Both halves of the fixed candidate are absent: there is no Compose-file ingestion layer, and there is no health-derivation/dependency-gating primitive in the C API — only the generic exec/lifecycle primitives #15 already confirmed work on a live WSLC host (`process.exec.recurring`, container create/start/stop/delete). |

## What this table does and does not show

None of the three alternatives was confirmed, to a primary-source
standard within this investigation's scope, to perform the fixed
operation for both containerd and WSLC with the same contract. Docker
Compose implements the contract but only reaches the Docker Engine API,
never WSLC. The WSLC SDK has no Compose-reading layer and no
health/dependency primitive at all — this is confirmed directly, from the
pinned header itself. Whether the containerd/nerdctl family implements or
drops the feature was not established here to the same standard, and this
document does not claim otherwise.

This absence of a confirmed existing solution is a fact about these three
alternatives. It is not, by itself, evidence that NAVISOMA must own an
irreducible, backend-neutral core to provide this: it is equally
consistent with the responsibility simply not having been prioritized by
any of the three, with a generic reusable component existing outside them,
or with a thin per-backend adapter approach turning out to be sufficient.
None of those possibilities was ruled out by this investigation.

## Integration responsibility, not a NAVISOMA-specific core claim

Health-gated dependency scheduling — deriving a health state from
repeated exec exit codes against `interval`/`retries`/`start_period`, then
gating a dependent service's creation on that state — is an **integration
responsibility that can be implemented on top of the generic exec and
lifecycle primitives** both fixed backends expose (a recurring
exec-with-exit-code mechanism, and container create/start). It is
described here as exactly that: an integration responsibility, not a
NAVISOMA-specific or irreducible core. This investigation does not
establish who should own building it, or that NAVISOMA specifically must.

- WSLC's exec and container lifecycle primitives that such an integration
  would depend on were directly confirmed working on a live host in #15
  (`process.exec.recurring`, `container_create`/`container_start`/etc.).
- containerd's exec and lifecycle primitives were not tested by #15 or by
  any Issue in this repository; their availability is not asserted here
  as a confirmed, real-machine capability, only as documented API surface
  referenced in the table above. containerd's real-machine capability is
  not used as evidence for this Issue's conclusion.

## Status

**#16 is closed as: no NAVISOMA-specific core established.** See
"Conclusion" above for the exact finding. No `proceed` is declared, no
other Issue is modified, and no new Issue was opened as part of this
closure.
