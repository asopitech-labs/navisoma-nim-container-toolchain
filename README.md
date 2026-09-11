# NAVISOMA

**Nim Container Toolchain**

Cross-platform container toolchain built in Nim, with native Compose and OCI implementations for containerd, WSL Containers, and other runtimes.

NAVISOMA is an umbrella project for building the missing container and cloud-native systems ecosystem around Nim. The goal is not to wrap Docker or reproduce a Docker-specific architecture. NAVISOMA treats open specifications such as the Compose Specification and OCI specifications as first-class interfaces, lowers them into an explicit execution model, and executes that model against multiple container backends.

A core implementation principle is to exploit Nim's native C/C++ interoperability rather than reimplement mature infrastructure. Stable C APIs, C++ libraries, generated bindings and thin C/C++ shims are preferred whenever they provide a better compatibility and maintenance boundary. New Nim implementations are created only where the reusable native ecosystem does not provide the required semantics.

## Goals

- Provide the same container and Compose-oriented workflow across Linux, WSL, macOS, and Windows with WSL Containers.
- Implement the NAVISOMA-specific semantic layers in Nim while directly reusing mature C/C++ protocol and systems libraries where appropriate.
- Treat containerd and WSL Containers as first-class execution backends behind a common semantic model.
- Build reusable Nim bindings, clients or libraries only where they form independently useful boundaries.
- Keep reusable specification, client and binding components independently usable outside NAVISOMA.
- Use real container orchestration as the integration and conformance workload rather than developing isolated libraries without an end-to-end consumer.

## Architecture

```text
compose.yaml
    |
    v
Compose frontend
    |
Canonical application model
    |
Execution graph / planner
    |
Unified container semantics
    |
    +----------------------+----------------------+
    |                                             |
containerd backend                           WSLC backend
    |                                             |
Linux / WSL / macOS VM                      Windows
```

Image building is modeled separately from container execution. BuildKit and other builders can therefore be selected independently of the runtime backend.

Below the semantic layer, NAVISOMA prefers mature native implementations such as gRPC Core/C++, official protobuf runtimes, JSON Schema C++ libraries, CNI, Lima and other established system components instead of recreating them in Nim.

## Ecosystem

NAVISOMA is intentionally expected to span multiple repositories where an independently useful boundary is demonstrated. The `nvsm-` prefix identifies repositories developed under the NAVISOMA umbrella while retaining the upstream specification or system name.

Current strong or conditional repository candidates include:

- `nvsm-compose-nim` — Compose Specification processing, semantic model, validation integration and normalization.
- `nvsm-wslc-client-nim` — WSL Container API bindings and safe Nim client.
- `nvsm-containerd-client-nim` — reusable containerd client facade if it is cleaner as a standalone package.
- `nvsm-buildkit-client-nim` — reusable BuildKit/LLB client if justified independently.
- `nvsm-oci-spec-nim` / `nvsm-oci-client-nim` — only if OCI models/registry behavior become substantial enough to justify separate packages.

A dedicated NAVISOMA gRPC, protobuf, JSON Schema, YAML, HTTP or TLS implementation is **not** the default plan. Mature existing Nim or C/C++ implementations are evaluated first.

## Design principles

### Specifications before vendor APIs

Compose Specification, OCI specifications, protobuf/gRPC protocols, and other open interfaces define the portable model. Docker compatibility may be useful, but Docker is not the architectural center of NAVISOMA.

### Reuse before reimplementation

Nim's `importc` / `importcpp` capabilities are first-class architectural tools. The preferred order is stable C ABI, narrow C++ API or thin shim, generated bindings/codegen, existing Nim library, and only then a new Nim implementation.

### One user model, multiple platform implementations

WSL Containers are not treated as an exceptional side path. Linux, WSL, macOS, and Windows expose the same NAVISOMA operations. Platform differences exist below the common execution model.

### Explicit execution graph

Compose dependencies and lifecycle operations are lowered into explicit actions such as image resolution, pull, build, network/volume creation, container creation, start, health waiting, exec, stop, and removal. This enables dependency scheduling, parallelism, dry-run/explain tooling, deterministic teardown, and incremental reconciliation.

### Capability-aware runtimes

Runtime features are represented explicitly. The planner can reason about capabilities such as networking modes, volume types, GPU support, image building, and port publication rather than assuming every backend implements an identical hidden feature set.

### Conformance first

Compatibility claims must be demonstrated against specifications and reference behavior. Compose work should use the Compose Specification, schema and examples, compose-go behavior/tests where appropriate, Docker Compose and nerdctl behavior where relevant, and real-world Compose files. OCI components should use upstream OCI conformance material where available.

## Research

The current research and architecture documents are maintained under [`docs/`](docs/):

- [`docs/project-vision.md`](docs/project-vision.md) — scope, motivation, goals, and non-goals.
- [`docs/ecosystem-research.md`](docs/ecosystem-research.md) — findings on the current Nim ecosystem and the areas NAVISOMA should reuse, strengthen, or implement.
- [`docs/reference-implementations.md`](docs/reference-implementations.md) — codebases to reuse or study, adoption boundaries, compatibility oracles, and source-code provenance policy.
- [`docs/native-library-reuse.md`](docs/native-library-reuse.md) — revised strategy centered on direct C/C++ reuse through Nim FFI, shims and generated bindings.
- [`docs/architecture.md`](docs/architecture.md) — proposed frontend, execution model, runtime/build boundaries, and platform architecture.
- [`docs/repository-strategy.md`](docs/repository-strategy.md) — multi-repository namespace and component boundaries.

## Status

NAVISOMA is currently in the research and architecture stage. Repository boundaries and implementation choices remain subject to evidence from compatibility, conformance, API stability, native-library packaging, maintenance activity, and real-world workload evaluation.

## License

See [`LICENSE`](LICENSE).
