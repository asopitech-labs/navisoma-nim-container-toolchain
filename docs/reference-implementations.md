# Reference Implementations and Reuse Strategy

## Purpose

NAVISOMA must not treat every subsystem as greenfield work. The container ecosystem already contains mature implementations whose code, tests, data models, execution boundaries, and failure handling encode years of production experience.

This document defines which projects should be studied, which parts should be reused directly, which should be reimplemented natively in Nim while preserving proven architecture, and which should act primarily as compatibility/conformance oracles.

The goal is not mechanical porting. It is to avoid unnecessary invention while still producing a coherent Nim-native ecosystem.

## Four adoption modes

Every external project should be assigned one or more explicit roles.

### A. Direct dependency / direct reuse

Use the existing implementation when:

- it already has a strong Nim API or a stable language-neutral process/API boundary;
- reimplementation adds little ecosystem value;
- compatibility and maintenance are better served by upstream reuse.

Examples: NimYAML; external CNI plugin binaries; an existing macOS VM manager.

### B. Architectural/code reference for native Nim implementation

Study the existing code deeply, preserve proven semantic boundaries and algorithms where suitable, but implement the NAVISOMA-facing library in Nim.

Examples: compose-go processing pipeline; nerdctl containerd orchestration; ORAS content graph; BuildKit client/LLB handling.

Code copied or translated from another project must only be used where its license and attribution requirements are satisfied. Design ideas, observable behavior, public specifications, and test vectors are not a license to copy arbitrary source without tracking provenance.

### C. Compatibility/conformance oracle

Run the external implementation against identical fixtures and compare semantic/canonical results.

Examples: compose-go and Docker Compose for Compose; OCI upstream conformance; cross-language protobuf/gRPC implementations.

### D. Boundary/anti-pattern reference

Some projects are valuable because they show what NAVISOMA should deliberately not couple together, or expose practical limitations that a clean abstraction must accommodate.

Examples: runtime-specific Compose feature gaps; runtime/build coupling; platform-specific CLI divergence.

---

# Priority reference set

## 1. compose-spec/compose-go

Repository: https://github.com/compose-spec/compose-go

Role: **B + C; highest-priority Compose reference**.

`compose-go` is the reference library for parsing and loading Compose YAML and is used by Docker Compose, nerdctl, Tilt, Kompose, Testcontainers-Go and others. Its value is not only its final Go structs. Its loader exposes the real semantic processing pipeline required by Compose.

### Study and adopt conceptually

- loader phase ordering;
- per-file interpolation;
- `include` processing before `extends`;
- multi-file merge and uniqueness enforcement;
- schema validation;
- default-value transforms;
- canonical transforms;
- normalization;
- path resolution and Windows path conversion;
- environment/profile/service selection handling;
- consistency validation;
- cycle detection and caching around include/extends;
- project-name normalization;
- distinction between raw/canonical model and typed `Project` binding.

The current loader explicitly exposes switches such as skip validation/interpolation/normalization/path resolution/include/extends/consistency/default values. This is strong evidence that NAVISOMA should keep these phases explicit rather than implement one opaque `loadCompose()` function.

### Do not adopt blindly

- Go-specific map/interface representation;
- option-pattern APIs purely because Go uses them;
- implicit filesystem/environment reads that make deterministic tooling difficult;
- internal types whose structure exists only for Go decoder convenience.

### NAVISOMA use

`nvsm-compose-nim` should aim for semantic parity while designing a Nim-appropriate typed API. compose-go should be a differential oracle: feed identical source/environment/path inputs into both implementations and compare normalized/canonical output.

The compose-go test corpus should be indexed by semantic area and reused as test scenarios where licensing permits, with provenance retained.

License: Apache-2.0.

---

## 2. Docker Compose v2 (`docker/compose`)

Repository: https://github.com/docker/compose

Role: **C + B for orchestration behavior and UX**.

Docker Compose is the dominant user-facing implementation of the Compose Specification. NAVISOMA should not copy Docker Engine coupling, but it should study how the Compose model becomes lifecycle operations.

### Study

- project/service selection and dependency traversal;
- create/start/stop/down semantics;
- service convergence and container recreation decisions;
- config hash / change detection;
- dependency conditions and health waits;
- scaling and replica identity;
- progress/event reporting;
- attach/log/exec behavior;
- failure propagation and teardown;
- project labels and resource ownership.

### Adopt

Primarily behavior, test scenarios and semantic expectations rather than Docker Engine API-specific code.

### Avoid

Do not define NAVISOMA's canonical runtime interface by copying Docker Engine objects. Docker Compose is an oracle for Compose UX, not NAVISOMA's internal runtime abstraction.

---

## 3. containerd/nerdctl

Repository: https://github.com/containerd/nerdctl

Role: **B + C; highest-priority containerd/Compose execution reference**.

nerdctl is especially relevant because it proves that a Docker-like/Compose UX can be implemented over containerd without Docker Engine. It supports `nerdctl compose` and documents both supported and unimplemented Compose behavior.

### Study deeply

- conversion from Compose services to runtime operations;
- resource naming and project labels;
- service config hashing and recreate decisions;
- CNI network creation/attachment;
- volume lifecycle;
- containerd namespaces;
- snapshotter selection;
- image pull/content handling;
- task/process creation;
- exec and TTY/I/O;
- event handling and cleanup;
- BuildKit separation for `build`;
- rootless integration;
- behavior when Compose features are unsupported by the backend.

### Adopt conceptually

The separation visible in nerdctl is highly relevant:

```text
Compose semantics
    -> container lifecycle
    -> containerd
    + CNI for networking
    + BuildKit for builds
    + RootlessKit where needed
```

NAVISOMA can use the same mature external subsystems without reproducing their implementation.

### Do not copy as architecture

nerdctl's purpose is Docker-compatible UX for containerd, so some internal boundaries are necessarily shaped by Docker CLI compatibility. NAVISOMA's semantic IR should remain runtime-neutral and capability-aware.

### Conformance value

nerdctl's explicit list of unsupported/incompatible Compose fields is useful for designing NAVISOMA's capability matrix and diagnostics.

License: Apache-2.0.

---

## 4. containerd/containerd

Repository: https://github.com/containerd/containerd

Role: **B + protocol source of truth**.

containerd's protobuf/gRPC APIs are the authoritative backend contract for the Linux/WSL/macOS-VM execution path.

### Study/adopt

- official `.proto` definitions rather than manually invented RPC types;
- namespaces;
- content service;
- images service;
- containers service;
- snapshots;
- tasks/processes;
- leases;
- events;
- streaming APIs;
- error/status conventions;
- lifecycle relationships between container metadata, snapshot and task.

### Reference code

Use containerd's Go client and nerdctl call paths to understand required call ordering, lease handling, content ingestion and cleanup. Generate or write Nim protocol types from the authoritative protobuf rather than reproducing Go structs.

### Critical rule

`nvsm-containerd-client-nim` is a containerd client, not a container runtime implementation. Do not recreate snapshotters, shim protocols or OCI runtimes unless a separate requirement is established.

---

## 5. Moby BuildKit

Repository: https://github.com/moby/buildkit

Role: **A at process/protocol boundary + B for client implementation**.

BuildKit should normally be used as the builder rather than reimplemented. It already provides a concurrent, cache-efficient, Dockerfile-independent solver. LLB is protobuf-serialized, graph-based, concurrently executable and designed as a low-level build representation.

### Adopt directly

- `buildkitd` as a builder backend;
- Dockerfile frontend where Dockerfile compatibility is required;
- existing cache/export/import semantics;
- existing OCI output support.

### Implement in Nim

`nvsm-buildkit-client-nim` may provide a native client for:

- Solve;
- LLB definitions;
- session attachment;
- local directory/file synchronization requirements;
- secrets/SSH;
- progress/status streams;
- cache imports/exports;
- output/export configuration.

### Study carefully

BuildKit's client demonstrates an important API invariant: a solve may use an explicit LLB definition or a frontend, and status is streamed separately. NAVISOMA's Build Action model should preserve this separation rather than force every build into Dockerfile semantics.

License: Apache-2.0.

---

# OCI and registry references

## 6. OpenContainers specifications

Repositories:

- https://github.com/opencontainers/image-spec
- https://github.com/opencontainers/runtime-spec
- https://github.com/opencontainers/distribution-spec

Role: **authoritative specification + C**.

These specifications, schemas, Go types and conformance material are upstream truth. NAVISOMA should not infer OCI behavior from Docker implementations when the OCI specification directly defines it.

### Adopt

- descriptor semantics;
- digest/content identity;
- image manifest/index/config structures;
- media types;
- platform matching;
- OCI image layout;
- runtime bundle/config semantics where relevant;
- Distribution endpoints/error semantics/referrers;
- upstream schema/conformance fixtures.

The image-spec repository also maintains a list of OCI implementations. Use that list as a research index rather than inventing algorithms in isolation.

### Important schema note

Not every OCI schema uses the latest JSON Schema draft. For example, current image-manifest schema material still declares JSON Schema draft-04. A generic Nim JSON Schema effort therefore must be driven by actual upstream draft requirements, not the assumption that Compose Draft 2020-12 alone covers all NAVISOMA needs.

---

## 7. containers/oci-spec-rs

Repository: https://github.com/containers/oci-spec-rs

Role: **B; language-native OCI model precedent**.

Rust's OCI specification implementation is useful for studying how a non-Go ecosystem maps OCI documents into typed language-native models.

### Study

- module split between OCI specification areas;
- typed enums/newtypes versus arbitrary strings;
- serialization/deserialization choices;
- builder/default ergonomics;
- error handling and validation;
- maintaining compatibility with upstream spec evolution.

### NAVISOMA use

Use it as evidence that `nvsm-oci-spec-nim` should be an independent, typed specification package rather than untyped JSON maps embedded in registry/runtime clients.

---

## 8. ORAS (`oras-go`, `rust-oci-client`, ORAS CLI)

Repositories:

- https://github.com/oras-project/oras-go
- https://github.com/oras-project/rust-oci-client
- https://github.com/oras-project/oras

Role: **B + C; primary OCI registry architecture reference**.

ORAS is particularly valuable because its abstraction is broader than "pull a Docker image." ORAS models OCI content as content-addressed descriptors and graphs and treats data movement as copy operations across targets/stores.

### Study/adopt conceptually

- content-addressable storage interface;
- descriptor-first APIs;
- graph/DAG traversal;
- target/store abstraction;
- copy-based push/pull semantics;
- referrers/artifact handling;
- verification by digest;
- registry compatibility behavior;
- separation between content model and HTTP transport.

### Why this matters

A simplistic `RegistryClient.pullImage(name)` API would prematurely bake container-image assumptions into `nvsm-oci-client-nim`. ORAS demonstrates a more general OCI artifact architecture and should strongly influence the Nim API.

`rust-oci-client` is also useful because it shows a non-Go implementation of the Distribution protocol with an actively developed Apache-2.0 codebase.

---

## 9. google/go-containerregistry

Repository: https://github.com/google/go-containerregistry

Role: **B + robustness reference**.

This project is useful for the operational details of real registries.

### Study

- authentication/keychain separation;
- transport wrapping;
- retries for network errors and 408/429/5xx responses;
- backoff and jitter;
- HTTP/2-enabled transport defaults;
- concurrency limiting;
- platform selection when resolving indexes;
- referrers fallback behavior;
- clean split between image models, `remote`, `transport`, and `authn`.

### Adopt conceptually

NAVISOMA's registry client should not begin with a naive one-request-per-operation HTTP client. Retry/auth/rate-limit/platform behavior should be designed from production precedent.

---

## 10. containers/image

Repository: https://github.com/containers/image

Role: **B + D**.

containers/image is a mature reference for moving container images among multiple transports (registries, OCI layouts, local container storage, daemon transports, archives, etc.).

### Study

- transport abstraction;
- copy pipeline;
- signature/policy boundaries;
- manifest conversion;
- source/destination interfaces;
- distinction between registry transport and local storage.

### NAVISOMA decision

Do not necessarily reproduce the entire multi-transport abstraction initially. Use it to ensure `nvsm-oci-client-nim` and the core model do not confuse "OCI image" with "remote registry object."

---

# Networking, rootless, storage, and platform references

## 11. CNI + containernetworking/plugins

Repositories:

- https://github.com/containernetworking/cni
- https://github.com/containernetworking/plugins

Role: **A + B**.

CNI is a language-agnostic specification with executable plugins. NAVISOMA should normally invoke existing CNI plugins rather than rewrite bridge, portmap, host-local IPAM, loopback and related networking implementations in Nim.

### Adopt directly

- CNI specification/config format;
- released plugin binaries;
- standard plugin chaining semantics.

### Implement in Nim

Only the orchestration/client side needed to:

- discover plugins/config;
- construct CNI requests;
- invoke plugins;
- parse results/errors;
- manage ADD/DEL/CHECK/GC lifecycle as required.

### Reference

CNI's design itself is instructive: networking is intentionally a narrow plugin interface separated from container execution. NAVISOMA should preserve that separation for its containerd backend.

---

## 12. RootlessKit

Repository: https://github.com/rootless-containers/rootlesskit

Role: **A + B; do not reimplement rootless Linux plumbing**.

RootlessKit already manages user/mount/network namespaces, subordinate UID/GID mapping and rootless networking mechanisms used by Docker, Podman, nerdctl and BuildKit.

### NAVISOMA use

- use RootlessKit where a rootless containerd/BuildKit deployment requires it;
- study state-directory, port/network, namespace and process lifecycle boundaries;
- do not implement a new user-namespace/fakeroot subsystem in Nim merely for stack purity.

License is compatible with common container ecosystem reuse patterns; maintain normal third-party attribution/provenance handling.

---

## 13. containers/storage

Repository: https://github.com/containers/storage

Role: **B + D**.

This is a useful alternative architecture reference to containerd snapshotters/content store. It explicitly separates layers, images and containers and supports multiple storage drivers.

### Study

- layer/image/container identity;
- parent layer graph;
- metadata versus large data;
- driver boundaries;
- configuration precedence;
- locking/state handling.

### NAVISOMA decision

Do not implement a new image/layer store as part of the initial architecture when containerd already provides content/snapshot management. Use containers/storage to understand the problem space and avoid accidentally implementing a partial, unsafe storage layer in the orchestration engine.

---

## 14. Lima

Repository: https://github.com/lima-vm/lima

Role: **A + B; primary macOS platform reference**.

Lima is a mature CNCF project focused on Linux VMs for container workloads. Its internal architecture separates `limactl`, hostagent, guestagent, drivers, config loading/defaulting/validation, provisioning and instance lifecycle.

### Adopt directly initially

Use Lima as the managed Linux VM layer on macOS rather than creating a hypervisor/VM manager.

### Study

- host/guest agent split;
- driver abstraction for virtualization backends;
- VM instance lifecycle;
- socket/port forwarding;
- filesystem sharing;
- configuration/default/validation pipeline;
- provisioning/guest agent deployment;
- separation of fast unit tests from VM-booting integration tests.

### NAVISOMA boundary

NAVISOMA should manage or connect to the VM at the platform layer, then communicate directly with its selected container runtime API. The orchestration core should not depend on Lima-specific types.

License: Apache-2.0.

---

# Nim-native foundations

## 15. flyx/NimYAML

Repository: https://github.com/flyx/NimYAML

Role: **A; default YAML candidate**.

NimYAML is pure Nim, stable, supports Nim 2.x, and reports passing the current YAML 1.2 test suite.

### Adopt directly unless a Compose-specific blocker is demonstrated

- YAML token/event/document parsing;
- scalar/collection representation as appropriate;
- source location support where available.

### Do not build

Do not create a NAVISOMA YAML parser just because Compose uses YAML.

Compose-specific merge/interpolation/include semantics belong in `nvsm-compose-nim`, not in NimYAML.

License: MIT.

---

## 16. status-im/nim-protobuf-serialization

Repository: https://github.com/status-im/nim-protobuf-serialization

Role: **A/B pending audit**.

Existing Nim protobuf work should be the first candidate for wire encoding/schema generation rather than creating `nvsm-protobuf-nim` automatically.

### Evaluate

- proto2/proto3/Editions coverage;
- oneof/maps/enums;
- unknown fields;
- well-known types;
- descriptors/reflection;
- code generation;
- gRPC service generation;
- compatibility with current containerd/BuildKit protobuf schemas;
- Unix-domain-socket transport integration requirements above protobuf.

### Decision rule

Prefer upstream fixes/extensions if the base architecture is viable. Fork/new project only if the required compatibility cannot realistically be achieved upstream.

---

## 17. Joubako

Repository: https://github.com/puffball1567/joubako

Role: **A/B pending audit; primary current Nim gRPC/HTTP reference**.

Joubako already provides a modern Nim async HTTP stack and integrates `nim-protobuf-serialization`; its documentation reports unary and streaming gRPC, HTTP/2 interoperability tests, retries, streaming, TLS, gzip and benchmark infrastructure.

### Study/adopt where suitable

- typed transport/result errors;
- standard `await` integration;
- HTTP/2 via mature underlying transport rather than rewriting HTTP/2;
- gRPC framing/status/trailers;
- streaming and backpressure;
- local interoperability tests;
- repeatable benchmark methodology;
- third-party license accounting.

### Mandatory NAVISOMA-specific audit

containerd requires local IPC semantics that typical internet gRPC clients do not exercise. Before declaring Joubako sufficient, test:

- Unix domain sockets;
- long-lived streaming APIs;
- metadata/status interoperability with containerd;
- cancellation/deadline propagation;
- flow control under task/event streams;
- BuildKit session behavior.

If gaps are small, contribute upstream. `nvsm-grpc-nim` should not be created merely because NAVISOMA wants its own namespace.

License: Apache-2.0.

---

# Windows / WSLC reference set

## 18. Microsoft WSL Container API and WSLC samples/source material

Primary sources:

- Microsoft WSL Container API documentation
- Microsoft WSL repository/issues/discussions
- supported C/C++ examples and headers when published

Role: **authoritative API source + B**.

### Adopt

- official C projection and ABI types;
- supported lifecycle calls;
- ownership/error/threading rules;
- capability definitions;
- version/preview checks.

### Implement in Nim

`nvsm-wslc-client-nim` should be a thin, safe Nim layer over the official ABI rather than a second container implementation.

### Reference the CLI but do not depend on it as the architecture

`wslc.exe` is useful as a behavior comparison and fallback/probing tool. If the SDK lacks a CLI feature, document the gap explicitly. Do not silently fork the user model into a Windows-only orchestration path.

Because WSLC is evolving, code should isolate ABI/version-specific details behind a narrow binding layer and capability model.

---

# Additional projects worth studying

## 19. Podman / containers ecosystem

Repositories include:

- https://github.com/containers/podman
- https://github.com/containers/image
- https://github.com/containers/storage

Role: **B + D**.

The Containers ecosystem is useful as a large non-Docker implementation showing separation of image transport, storage, OCI runtime execution, networking and rootless concerns.

Study architecture and error cases, but avoid reproducing its entire stack when NAVISOMA's chosen execution backend is containerd/WSLC.

## 20. runc and crun

Repositories:

- https://github.com/opencontainers/runc
- https://github.com/containers/crun

Role: **protocol/runtime boundary reference, not a NAVISOMA reimplementation target**.

Study OCI Runtime Specification translation and lifecycle semantics when necessary to understand containerd behavior. NAVISOMA should normally let containerd/shims invoke OCI runtimes rather than call runc/crun directly.

## 21. Finch / Colima / Rancher Desktop

Role: **platform UX and packaging references**.

These projects demonstrate different ways to package a VM, container runtime, builder, networking and Docker-compatible UX on desktop operating systems.

Study:

- automatic VM bootstrap;
- configuration/state directories;
- upgrade/migration;
- socket exposure;
- resource configuration;
- diagnostics.

Do not assume their Docker-compatibility architecture must become NAVISOMA's internal API.

---

# Recommended reuse matrix

| NAVISOMA area | Primary reference | Adoption mode | Recommended action |
|---|---|---|---|
| YAML | NimYAML | A | Depend on it unless a measured blocker exists |
| Compose frontend | compose-go | B+C | Native Nim implementation; differential oracle |
| Compose lifecycle UX | Docker Compose | B+C | Reuse behavior/test cases, not Docker Engine model |
| Compose on containerd | nerdctl | B+C | Study code paths deeply; reuse CNI/BuildKit choices |
| OCI models | OCI specs + oci-spec-rs | B+C | Native typed Nim spec package |
| OCI registry | ORAS + go-containerregistry + rust-oci-client | B+C | Native Nim client with proven CAS/auth/retry design |
| Protobuf | nim-protobuf-serialization | A/B | Reuse/upstream first |
| gRPC | Joubako | A/B | Validate local IPC/streaming; upstream first |
| containerd | containerd + nerdctl | B | Native Nim client generated from official proto |
| Builds | BuildKit | A+B | Run BuildKit; implement Nim client, not solver |
| Linux networking | CNI/plugins | A+B | Execute standard plugins; implement only client/orchestration |
| Rootless | RootlessKit | A | Reuse; do not rewrite namespace machinery |
| Local layer storage | containerd; containers/storage as reference | D/B | Do not invent storage subsystem |
| macOS VM | Lima | A+B | Reuse initially; isolate behind platform boundary |
| Windows containers | WSL Container API | A/B | Thin native C ABI binding + semantic lowering |
| OCI runtime | runc/crun | A through containerd | Do not call/reimplement directly initially |

---

# Source-code reuse policy

## Prefer specifications and generated interfaces over translated implementation code

When an authoritative schema/protobuf/C header exists, generate or implement from that source rather than translating a Go/Rust wrapper line by line.

Examples:

- containerd: use official protobuf as source of protocol truth;
- BuildKit: use official protobuf/LLB definitions;
- WSLC: use official C ABI headers;
- OCI: use specification/schema documents;
- Compose: implement specification semantics while using compose-go as behavioral reference.

## Track provenance for copied or substantially translated code

If source code, algorithms with distinctive implementation expression, or test fixtures are copied/adapted:

- record source repository/path/commit;
- record license;
- retain required copyright/license notices;
- add third-party attribution where required;
- avoid mixing incompatible licenses into reusable NAVISOMA packages.

Many major reference projects above use Apache-2.0, while NimYAML uses MIT. That makes reuse comparatively feasible, but every dependency/file must still be checked at the actual point of adoption.

## Do not copy internal bugs as compatibility requirements

Differential testing can reveal mismatches, but differences must be classified:

```text
spec-required
reference behavior intentionally followed
backend limitation
reference implementation bug
NAVISOMA bug
undefined/ambiguous
```

NAVISOMA should not reproduce an upstream bug merely to get a zero diff unless compatibility explicitly requires it.

---

# Research workflow before implementing a subsystem

For each new NAVISOMA component, implementation work should begin with a reference audit:

1. Identify authoritative specification/API sources.
2. Identify at least one mature production implementation.
3. Identify an implementation in a second language/ecosystem when available.
4. Locate upstream conformance and regression tests.
5. Trace one complete real operation through the reference code.
6. Record responsibility boundaries and external dependencies.
7. Decide A/B/C/D adoption mode.
8. Record license/provenance constraints.
9. Only then design the Nim API.
10. Add differential/interoperability tests before declaring compatibility.

This workflow should be part of issue acceptance criteria for all major `nvsm-*` repositories.

---

# Immediate code-reading priorities

The highest-value code-reading sequence is:

1. `compose-go/loader` and its interpolation/override/transform/validation packages.
2. Docker Compose create/convergence/dependency lifecycle code.
3. nerdctl Compose implementation, then the underlying `run`, CNI, volume and containerd code paths.
4. containerd client API usage for pull -> snapshot -> container -> task -> exec -> cleanup.
5. ORAS Go and Rust OCI client for CAS/registry design.
6. BuildKit `client`, `solver/pb` and session/progress APIs.
7. Lima internals for host/guest/platform separation.
8. CNI client/plugin execution and RootlessKit lifecycle.
9. Joubako + nim-protobuf-serialization against a real containerd endpoint.
10. WSLC official C headers/examples against the canonical runtime operations.

The result of each code-reading pass should be a short design record linked from the implementing issue. This prevents implementation agents from repeatedly rediscovering the same upstream architecture or inventing new abstractions without reference to established practice.

## Current conclusion

NAVISOMA should create new code primarily at the boundaries where a Nim-native reusable implementation is genuinely missing:

- Compose semantic frontend/model;
- OCI typed models where no adequate Nim package exists;
- OCI registry client if existing Nim HTTP libraries are sufficient foundations but no registry implementation exists;
- containerd client bindings and high-level helpers;
- WSLC bindings/client;
- BuildKit client;
- canonical execution graph/planner;
- cross-backend capability/reconciliation layer.

It should **not** create from scratch:

- YAML parsing;
- Linux container networking plugins;
- rootless namespace machinery;
- a Linux VM/hypervisor for macOS;
- a build solver equivalent to BuildKit;
- a containerd replacement;
- an OCI runtime equivalent to runc/crun;
- protobuf/gRPC if existing Nim implementations can be brought to the required interoperability level.

The default rule is therefore: **specification first, mature reference second, reuse before rewrite, native Nim implementation only where it creates a real reusable boundary.**
