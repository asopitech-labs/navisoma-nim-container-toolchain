# NAVISOMA

**Nim Container Toolchain**

Cross-platform container toolchain built in Nim, with native Compose and OCI implementations for containerd, WSL Containers, and other runtimes.

NAVISOMA is an umbrella project for building the missing container and cloud-native systems ecosystem around Nim. The goal is not to wrap Docker or reproduce a Docker-specific architecture. NAVISOMA treats open specifications such as the Compose Specification and OCI specifications as first-class interfaces, lowers them into an explicit execution model, and executes that model against multiple container backends.

## Goals

- Provide the same container and Compose-oriented workflow across Linux, WSL, macOS, and Windows with WSL Containers.
- Implement Compose and OCI semantics natively in the Nim ecosystem rather than depending on Docker-specific implementations.
- Treat containerd and WSL Containers as first-class execution backends behind a common semantic model.
- Build reusable Nim libraries for specifications and protocols that are currently missing or fragmented in the ecosystem.
- Keep reusable specification, protocol, client, and binding implementations independently usable outside NAVISOMA.
- Use real container orchestration as the integration and conformance workload for the ecosystem rather than developing isolated libraries without an end-to-end consumer.

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

## Ecosystem

NAVISOMA is intentionally expected to span multiple repositories. The `nvsm-` prefix identifies repositories developed under the NAVISOMA umbrella while retaining the upstream specification or system name.

Planned or investigated repository boundaries include:

- `nvsm-compose-nim` — Compose Specification parser, processing pipeline, semantic model, validation, and normalization.
- `nvsm-oci-spec-nim` — OCI specification types, serialization, validation, descriptors, manifests, image indexes, and related models.
- `nvsm-oci-client-nim` — OCI Distribution / registry client.
- `nvsm-grpc-nim` — reusable gRPC infrastructure where existing Nim implementations do not satisfy the required compatibility and maintenance criteria.
- `nvsm-containerd-client-nim` — native containerd API client.
- `nvsm-buildkit-client-nim` — BuildKit and LLB client/tooling.
- `nvsm-wslc-client-nim` — WSL Container API bindings and client.

These repository boundaries are architectural candidates, not a requirement to reimplement working Nim libraries. Existing projects are evaluated first and reused or improved when appropriate.

## Design principles

### Specifications before vendor APIs

Compose Specification, OCI specifications, protobuf/gRPC protocols, and other open interfaces define the portable model. Docker compatibility may be useful, but Docker is not the architectural center of NAVISOMA.

### One user model, multiple platform implementations

WSL Containers are not treated as an exceptional side path. Linux, WSL, macOS, and Windows expose the same NAVISOMA operations. Platform differences exist below the common execution model.

### Explicit execution graph

Compose dependencies and lifecycle operations are lowered into explicit actions such as image resolution, pull, build, network/volume creation, container creation, start, health waiting, exec, stop, and removal. This enables dependency scheduling, parallelism, dry-run/explain tooling, deterministic teardown, and incremental reconciliation.

### Capability-aware runtimes

Runtime features are represented explicitly. The planner can reason about capabilities such as networking modes, volume types, GPU support, image building, and port publication rather than assuming every backend implements an identical hidden feature set.

### Reusable Nim infrastructure

A JSON Schema validator, OCI model, gRPC stack, containerd client, or WSLC binding should not be buried inside the final CLI when it has independent ecosystem value. NAVISOMA is intended to leave useful Nim infrastructure behind even when components are used independently.

### Conformance first

Compatibility claims must be demonstrated against specifications and reference behavior. Compose work should use the Compose Specification, schema and examples, compose-go behavior/tests where appropriate, Docker Compose and nerdctl behavior where relevant, and real-world Compose files. OCI components should use upstream OCI conformance material where available.

## Research

The current research and architecture documents are maintained under [`docs/`](docs/):

- [`docs/project-vision.md`](docs/project-vision.md) — scope, motivation, goals, and non-goals.
- [`docs/ecosystem-research.md`](docs/ecosystem-research.md) — findings on the current Nim ecosystem and the areas NAVISOMA should reuse, strengthen, or implement.
- [`docs/reference-implementations.md`](docs/reference-implementations.md) — codebases to reuse or study, adoption boundaries, compatibility oracles, and source-code provenance policy.
- [`docs/architecture.md`](docs/architecture.md) — proposed frontend, execution model, runtime/build boundaries, and platform architecture.
- [`docs/repository-strategy.md`](docs/repository-strategy.md) — multi-repository namespace and component boundaries.

## Status

NAVISOMA is currently in the research and architecture stage. Repository boundaries and implementation choices remain subject to evidence from compatibility, conformance, API stability, maintenance activity, and real-world workload evaluation.

## License

See [`LICENSE`](LICENSE).
