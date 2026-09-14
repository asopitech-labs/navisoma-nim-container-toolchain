# Irreducible core counterfactual — Issue #16

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
| **containerd / nerdctl family** | Parsed but ignored. `nerdctl compose up` logs `WARN[0000] Ignoring: service db: [HealthCheck]` and `WARN[0000] Ignoring: service adminer: depends_on: db: condition service_healthy` — the YAML is read, the semantics are dropped. [[containerd/nerdctl#2386]](https://github.com/containerd/nerdctl/issues/2386), tracked as an open enhancement request as of this writing: [[containerd/nerdctl#4157]](https://github.com/containerd/nerdctl/issues/4157). containerd's own gRPC API (the layer nerdctl is built on) exposes `images`, `tasks`, `content`, `snapshots`, `namespaces`, `diff`, `version`, `events`, `leases`, `introspection` — no compose or health-gated-scheduling service. [[containerd.io docs]](https://containerd.io/docs/2.2/getting-started/) | Yes (nerdctl is containerd-native). | No. nerdctl targets containerd exclusively; no WSLC backend exists. | N/A — the feature does not execute, so there is no derived failure contract for it to keep consistent. | Health state is never derived and the dependent service is never gated on it: `healthcheck:` and `depends_on: condition: service_healthy` are recognized syntax with no runtime effect. Even if this were fixed, nerdctl still has no WSLC target. |
| **WSLC SDK / `wslc` family** | No. The pinned `wslcsdk.h` (Microsoft.WSL.Containers 2.9.9, SHA-256 `e2867c46e51db62b7307dc858c2f5cd767d298760b272ad19a161145bd1bbba1`) was read in full (682 lines) for #14/#15 and declares no Compose-file parsing, no `healthcheck` type, and no multi-container dependency primitive of any kind — only single-container lifecycle (`WslcCreateContainer`/`WslcStartContainer`/.../`WslcDeleteContainer`) plus a generic `WslcCreateContainerProcess` exec primitive; #14's own Case 2 analysis states plainly that *"no health concept exists inside the backend itself."* [[this repo, `semantic-core-gate.md`, Case 2 table]](semantic-core-gate.md) [[this repo, `wslc-capability-gate.md`, "682 lines, read in full"]](wslc-capability-gate.md) Corroborating secondary source: *"Compose, Swarm, buildx bake and restart policies have no equivalent yet in the native wslc tool."* [[wslcontainers.com]](https://wslcontainers.com/) | No. WSLC is a separate Windows-native session/VM-based container runtime, unrelated to containerd (confirmed directly: #15's probe never touched containerd in any form). | Yes, trivially (it is the WSLC family). | N/A — no cross-backend targeting exists for this family to begin with. | Both halves of the fixed candidate are absent: there is no Compose-file ingestion layer, and there is no health-derivation/dependency-gating primitive in the C API — only the generic exec/lifecycle primitives #15 already confirmed work (`process.exec.recurring`, container create/start/stop/delete). |

## Stop condition check

None of the three alternatives performs the fixed operation for **both**
containerd and WSLC with the same contract:

- Docker Compose never reaches WSLC at all, and reaches containerd only
  indirectly through a different (Docker Engine) API surface.
- nerdctl/containerd reaches only containerd, and — independent of the
  WSLC question — does not implement the health-gate semantics at all;
  it parses and discards them (primary-sourced above).
- The WSLC SDK/CLI family reaches only WSLC, and has neither a
  Compose-reading layer nor a health/dependency primitive to gate with.

**Candidate is not absent.** Recording the missing handoff and one core
candidate below, per the deliverable instructions; no `proceed` is
declared, no other Issue is modified, and no second candidate is
described.

## One core candidate

- **Input**: a service's `healthcheck:` stanza (`test`, `interval`,
  `timeout`, `retries`, `start_period`) and another service's
  `depends_on: <name>: condition: service_healthy` edge, as already
  represented in the Canonical Model per
  [`semantic-core-gate.md`](semantic-core-gate.md) Case 2.
- **Output**: a derived health state (`starting` / `healthy` /
  `unhealthy`) for the checked service, and a scheduling decision (hold /
  release) for the dependent service's create/start actions, expressed
  identically regardless of which backend is underneath.
- **Backend primitives it would be built from**: only the generic,
  already-confirmed-present primitives on both fixed backends — recurring
  exec-into-container with a retrievable exit code (containerd: task
  `Exec`; WSLC: `WslcCreateContainerProcess`/`WslcGetProcessExitCode`,
  both confirmed working in #15) and container lifecycle create/start.
  Neither backend, nor any of the three investigated alternatives, derives
  health or gates on it itself.
- **Missing handoff**: the translation from "N periodic exec exit codes,
  interpreted against interval/retries/start_period" to "a named health
  state that a scheduler gates a *different* service's creation on" does
  not exist as a reusable, backend-neutral component in any of the three
  investigated alternatives. Docker Compose has this exact translation,
  but only wired to the Docker Engine API, not to containerd's or WSLC's
  own client APIs.
- **Falsifier**: this candidate is not NAVISOMA's core if either (a) a
  generic, backend-pluggable implementation of this exact
  health-derivation-and-gating translation already exists and both
  containerd and WSLC could be wired to it as plain exec/lifecycle
  drivers, with no NAVISOMA-authored state machine in between, or (b) the
  three investigated alternatives, used together with only thin
  backend-specific adapters (no shared scheduling code), can be shown to
  converge on identical health-state derivation and dependent-service
  gating behavior for the fixed candidate — including matching edge-case
  behavior (a check that starts failing then recovers during
  `start_period`, retries-exhausted attribution) — without a third,
  actively-coordinating component. Neither was found during this
  investigation; both remain open questions a future, separately-approved
  investigation could test.
