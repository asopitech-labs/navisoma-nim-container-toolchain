# NAVISOMA Repository Strategy

## Why multiple repositories

NAVISOMA is intended to produce reusable Nim infrastructure, not a monolith whose useful libraries can only be consumed through the final container CLI.

Repository boundaries should follow independently useful specifications, protocols, and external APIs. They should not be created merely to make the project graph visually symmetrical.

## Namespace

Repositories developed as part of the NAVISOMA umbrella use the short prefix:

```text
nvsm-
```

The prefix solves two problems:

1. Names such as `compose-nim` or `grpc-nim` can look like official language implementations maintained by the upstream project.
2. NAVISOMA repositories remain searchable as one family without replacing recognizable upstream terminology with opaque component codenames.

Example:

```text
nvsm-compose-nim
```

means the NAVISOMA project's Nim implementation of the Compose Specification.

## Main repository

```text
navisoma-nim-container-toolchain
```

Responsibilities:

- umbrella documentation and research
- integrated CLI/toolchain
- canonical application/execution model
- planner/scheduler
- backend abstraction
- platform orchestration
- integration and end-to-end conformance workloads

Reusable specification/protocol implementations should move to independent repositories once their boundaries are justified.

## Candidate repositories

### `nvsm-compose-nim`

Purpose: native Compose Specification implementation for Nim.

Candidate responsibilities:

- Compose document loading
- interpolation
- merge semantics
- include/extends
- schema integration
- defaults
- normalization
- path handling
- consistency validation
- canonical Compose project model

Precedent: the Compose ecosystem's `compose-go` demonstrates the value of a reusable language-native Compose model independent of a single CLI.

### `nvsm-oci-spec-nim`

Purpose: language-native OCI specification models.

Candidate responsibilities:

- descriptors
- digests
- media types
- manifests
- image indexes
- image configuration
- platforms
- image layouts
- serialization/validation

The `oci-spec-rs` project is a useful naming and architectural precedent for a language-native OCI specification package.

### `nvsm-oci-client-nim`

Purpose: OCI Distribution/registry client.

Candidate responsibilities:

- registry API discovery
- authentication flows
- manifests
- blobs
- uploads/downloads
- tags
- referrers
- digest verification

This remains separate from static OCI specification models because it introduces transport, authentication, streaming and retry concerns.

### `nvsm-grpc-nim`

Purpose: reusable gRPC implementation if a new repository is actually required.

This repository must not be created until existing Nim projects such as Joubako and protobuf/serialization libraries have been evaluated. Upstream contribution is preferable when it can satisfy NAVISOMA's requirements.

### `nvsm-containerd-client-nim`

Purpose: native Nim client for containerd's public APIs.

Candidate responsibilities:

- generated/maintained API bindings
- transport
- namespaces
- images/content
- containers/tasks/processes
- snapshots
- leases/events
- streaming helpers

This is a containerd client, not a reimplementation of containerd.

### `nvsm-buildkit-client-nim`

Purpose: BuildKit/LLB client and supporting types.

Candidate responsibilities:

- BuildKit API bindings
- LLB representation/tooling
- solve/build operations
- progress/events
- import/export/cache interfaces required by NAVISOMA

This is a build client, not a BuildKit solver reimplementation unless a future project explicitly chooses otherwise.

### `nvsm-wslc-client-nim`

Purpose: Nim bindings/client for the WSL Container API.

Candidate responsibilities:

- C ABI bindings where appropriate
- safe Nim ownership/lifecycle wrappers
- session/container/process operations
- volume/network/port configuration
- capability discovery/normalization
- Windows-specific error translation

This is the primary Windows execution integration for NAVISOMA.

## Repositories that should not be assumed

The umbrella must not automatically create:

```text
nvsm-yaml-nim
nvsm-protobuf-nim
nvsm-http-nim
```

Nim already has meaningful implementations in these areas. A new repository requires evidence that existing projects cannot meet the specification, maintenance, portability, or API requirements and cannot reasonably be improved upstream.

The same rule applies to `nvsm-grpc-nim`.

## Package naming

GitHub repository names and Nimble package/module names do not have to be identical. The repository namespace communicates project provenance; imported module names should remain ergonomic and should avoid unnecessarily leaking umbrella branding into generic APIs.

For example, a repository may be:

```text
nvsm-compose-nim
```

while its public Nim module namespace can be designed around `compose` or another collision-safe package name according to Nimble availability and ecosystem conventions.

Package names require a separate collision survey before publication.

## Dependency policy

Repositories should depend downward on specifications/protocols, never upward on the NAVISOMA product.

Expected direction:

```text
compose library ---------------------+
                                     |
OCI spec -> OCI client --------------+
                                     |
protobuf/gRPC -> containerd client --+--> NAVISOMA
             -> BuildKit client -----+
                                     |
WSLC client -------------------------+
```

This allows each package to be used by unrelated Nim applications.

## Creation criteria

A candidate repository should be created when all of the following are true:

1. The responsibility has a coherent independent API.
2. It has independent users beyond NAVISOMA or is a clean binding to an external specification/API.
3. Existing Nim projects do not adequately cover the requirement, or the work has been intentionally split/upstreamed with maintainers.
4. There is a conformance/interoperability test strategy.
5. The repository name has been checked for collision with relevant upstream projects, GitHub repositories and Nimble packages.

## Versioning

Independent repositories should version according to their own compatibility surface and upstream specification/API evolution. NAVISOMA should not require all ecosystem repositories to share one synchronized version number.

A breaking Compose API change should not force a WSLC binding major-version change, for example.

## Governance and provenance

Each repository README should clearly state:

- that it is developed under the NAVISOMA project;
- whether it is an independent implementation or binding/client;
- that it is not an official upstream project unless upstream ownership actually changes;
- which upstream specification/API versions it targets;
- current conformance/compatibility status.

This is particularly important for names containing `compose`, `oci`, `grpc`, `containerd`, `buildkit`, or `wslc`.
