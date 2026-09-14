# Semantic core gate — Issue #14

## Purpose

This is a semantic-model spike, not an implementation. It tests whether a
single, backend-neutral Canonical Model and Execution Graph can carry a fixed
set of Compose intents down to two structurally different backends —
containerd and WSLC — without ever asking a user to perform a different
sequence of operations depending on which backend is selected.

It does **not** implement a Compose parser, a gRPC service, a C++ shim, or a
Nim binding. It fixes three representative Compose fragments, lowers each
canonical resource/action to both backends on paper, and classifies every
backend difference found along the way.

This document is the evidence artifact for #14, a child of #13. Its
classification of unresolved WSLC capabilities is the input to #15 ("verify
the WSLC capabilities required by that model on a live host").

## Method

For each of the three fixed cases below:

1. A short Compose fragment is frozen as the scenario input.
2. Every resource or action the fragment implies is listed as a row in a
   lowering table, in the vocabulary already established by
   [`docs/architecture.md`](../architecture.md) (project, service, network
   attachment, volume, published port, health/dependency condition,
   image/build source) — never in containerd, WSLC, Docker, gRPC, or
   protobuf vocabulary. Those vendor terms appear only inside the
   `containerd lowering` / `WSLC lowering` columns, where they are expected.
3. Each row records the dependency that must already hold before the
   canonical action can run, how each backend would realize it, and a single
   `Result` classification.
4. A backend-neutral action order is written below each table, using only
   the lifecycle vocabulary from `docs/architecture.md` §3 (resolve/build,
   create prerequisites, create, start, wait, stop, remove).

### Result values

Only one of these three values is used, ever:

- **supported** — both backends realize the canonical action with
  equivalent semantics; no capability probe or user-visible difference is
  needed.
- **capability-gated: `<capability>` — `<normalized diagnostic>`** — the
  canonical action is representable on both backends, but at least one
  backend's ability to satisfy it is not guaranteed by specification alone.
  The planner must query a named capability before scheduling the action and
  fail with the stated normalized diagnostic code if the capability is
  absent — the user's Compose file and the action order do not change
  either way. Confirming or refuting each `<capability>` on a real WSLC host
  is exactly the job handed to #15.
- **scope-breaking: `<reason>`** — the canonical action cannot be expressed
  without a backend-specific type leaking into the model, or satisfying it
  on one backend would require the user to run a different sequence of
  operations than on the other. No workaround is designed for a
  scope-breaking row; it is recorded as a counterexample against the core
  claim.

---

## Case 1 — image service, named network, named volume, published TCP port

```yaml
services:
  web:
    image: registry.example.com/web:1.4.0
    networks:
      - frontend
    volumes:
      - web-data:/var/lib/web
    ports:
      - "8080:80"
networks:
  frontend: {}
volumes:
  web-data: {}
```

| Canonical resource/action | Required dependency | containerd lowering | WSLC lowering | Result |
|---|---|---|---|---|
| Network `frontend` (create/remove) | none (project-scoped prerequisite) | supported — plugin-backed namespace scoped to the project; removed once its last attached container is gone | uncertain whether the backend exposes a project-scoped named network object that this project's services can attach to for connectivity | capability-gated: `network.named` — `E-NET-NAMED-UNSUPPORTED` |
| Volume `web-data` (create/remove) | none | supported — managed persistent mount with a lifecycle independent of any one container | uncertain whether the backend offers a managed named-volume abstraction, versus only host-path bind mounts | capability-gated: `volume.named.persistent` — `E-VOL-NAMED-UNSUPPORTED` |
| Image resolve (`web`) | none | supported — OCI registry pull into the content store | supported — OCI image resolution is a baseline capability of any OCI-conformant backend, not a Windows/WSL-specific one | supported |
| Container create (`web`), attached to `frontend`, mounting `web-data` | image resolved; `frontend` created; `web-data` created | supported — OCI spec composed with the namespace + mount entries | create itself is baseline; the attachment inherits whichever gate its network/volume row resolves to | supported (inherits the network/volume rows' gate, not itself gated) |
| Container start (`web`) | container created | supported | supported | supported |
| Publish TCP port 8080→80 | container created and attached to `frontend` | supported — mature host-forwarding path (portmap-class plugin) | host-to-guest TCP forwarding fidelity depends on the WSL networking mode (NAT vs. mirrored) active at start time and must be probed, not assumed constant across hosts | capability-gated: `port.publish.tcp` — `E-PORT-PUBLISH-MODE-DEPENDENT` |
| Wait for ready (running) | container started | supported | supported | supported |
| Container stop | ready-state reached or shutdown requested | supported | supported | supported |
| Container remove, then remove `web-data` / `frontend` | container stopped; no other consumers of the volume/network | supported | remove path inherits whatever gate its create-side row resolved to | supported (inherits the network/volume rows' gate) |

**Scope note on `network.named`.** Case 1's network requirement is limited
to representing `frontend` as a Canonical Model resource with a lifecycle
(create/remove) that this project's services can attach to for
connectivity, as a backend capability. It does **not** include isolating
`frontend` from any other, unrelated Compose project, and it does not
include supporting more than one independently-segmented network within a
single project. Both of those are potential **future platform promises**,
to be fixed as their own separate scenario if ever taken up, not part of
this gate's minimal core. Reason: this gate judges whether NAVISOMA needs
its own semantic core, not whether an existing runtime's isolation
mechanism exists or not.

**Action order (backend-neutral):**
`create prerequisites (network `frontend`, volume `web-data`) → resolve image (`web`) → create container → start → publish port → wait (running) → stop → remove container → remove volume → remove network`

---

## Case 2 — healthcheck and a `service_healthy` dependency

```yaml
services:
  db:
    image: registry.example.com/db:14
    healthcheck:
      test: ["CMD", "pg_isready", "-U", "app"]
      interval: 5s
      timeout: 3s
      retries: 5
  api:
    image: registry.example.com/api:2.1.0
    depends_on:
      db:
        condition: service_healthy
```

| Canonical resource/action | Required dependency | containerd lowering | WSLC lowering | Result |
|---|---|---|---|---|
| Resolve image + create container (`db`) | none | supported | supported | supported |
| Start container (`db`) | `db` created | supported | supported | supported |
| Health probe execution (recurring exec of the test command inside `db`) | `db` running | supported — a NAVISOMA-owned interval/timeout/retries scheduler drives the backend's generic exec-process operation and reads its exit code; no health concept exists inside the backend itself | exec-into-a-running-container with reliable exit-code and timeout reporting on this backend has not been confirmed, and is a precondition for deriving health at all | capability-gated: `process.exec.recurring` — `E-HEALTH-EXEC-UNVERIFIED` |
| Health state derivation (starting/healthy/unhealthy from `interval`/`retries`/`start_period`) | probe results from the row above | supported — pure planner-owned state machine, no backend type involved | supported — identical state machine, only its input probe results are gated above | supported |
| `depends_on: db: condition: service_healthy` gate on `api` | `db` health state == healthy | supported — execution-graph ordering constraint, not a backend call | supported — same graph-level constraint; blocked only insofar as the health state it reads is itself gated | supported (inherits the health-probe row's gate transitively) |
| Resolve image + create + start (`api`) | gate above satisfied | supported | supported | supported |
| Teardown: stop/remove `api`, then stop/remove `db` | reverse of the creation/dependency order above | supported | supported | supported |

**Action order (backend-neutral):**
`resolve/create `db` → start `db` → wait (health: starting → healthy) → [gate] → resolve/create `api` → start `api` → wait (running) → stop `api` → remove `api` → stop `db` → remove `db``

---

## Case 3 — `build:` with the generated image consumed by a runtime action

```yaml
services:
  worker:
    build:
      context: ./worker
      dockerfile: Dockerfile
    image: registry.example.com/worker:ci
```

| Canonical resource/action | Required dependency | containerd lowering | WSLC lowering | Result |
|---|---|---|---|---|
| Build image from context, tagged `worker:ci` | build context and Dockerfile present | supported — the builder writes its output directly into the same content/image store the runtime reads from | uncertain whether the builder's output store is directly visible to this backend, or whether a NAVISOMA-owned internal import/transfer action must be inserted between build and create; if so, that action stays inside the lowering and is never exposed to the user or the Compose file | capability-gated: `build.imagestore.import` — `E-BUILD-IMAGESTORE-UNRESOLVED` |
| Build-to-run image handoff (`worker`'s run image == build output tag) | build succeeded | supported — the canonical model only needs "this service's image identity equals the build output's identity"; resolution mechanics stay inside the lowering | supported, contingent on the import capability above; the handoff itself introduces no new type | supported (inherits the build row's gate) |
| Container create (`worker`) from the resolved image | handoff resolved | supported | supported, contingent on the import above having completed if required | supported (inherits the build row's gate) |
| Container start | container created | supported | supported | supported |
| Wait for ready (running) | container started | supported | supported | supported |
| Stop / remove container | ready-state reached | supported | supported | supported |

**Action order (backend-neutral):**
`build (`worker`) → resolve run image from build output → create container → start → wait (running) → stop → remove`

---

## Cross-case findings

- No row in any of the three cases required a containerd-, WSLC-, Docker-,
  gRPC-, or protobuf-shaped concept inside a canonical resource/action name
  or an action-order step. Every vendor-shaped term stays inside a
  `lowering` column, as intended.
- No row was classified **scope-breaking**. In every case where the two
  backends might diverge — the named network object, named volume
  persistence, TCP port publish under variable WSL networking modes,
  recurring exec-based health probing, and build-output image import — the
  divergence is representable as a named capability plus, at most, an
  internal lowering-adapter action (e.g. an image import step) that the
  user never sees and that does not change the action order.
- Every **capability-gated** row on the WSLC side is gated because its
  underlying capability is *unverified*, not because it is known to be
  absent. That is a deliberate scope boundary of #14: this spike fixes what
  must be true of WSLC for the common model to hold; #15 is the check
  against a live host. A capability that #15 finds genuinely and
  irreducibly absent — with no way to fail explicitly without also forcing
  a different user-facing sequence of operations — would be the actual
  counterexample this gate is designed to catch, and would move that row
  from capability-gated to scope-breaking retroactively.

## Recommendation for #13

A candidate backend-neutral Canonical Model / Execution Graph could be
described for all three fixed cases, and #15 went on to collect real WSLC
adapter capability evidence against the five capabilities this spike
identified (`network.named`, `volume.named.persistent`,
`port.publish.tcp`, `process.exec.recurring`, `build.imagestore.import`).
That evidence did not establish that this candidate model is a
NAVISOMA-specific semantic core irreducible to existing Compose/runtime/
adapter combinations — see
[`gate-13-decision.md`](gate-13-decision.md) for the gap that surfaced
(the `network.named` row assumes a project-scoped network resource with
its own create/remove lifecycle; WSLC's C API has no such object at all).
On that basis, **#13 concluded `revise the common semantic contract` and
is closed.**
