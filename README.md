# NAVISOMA

**Nim Container Toolchain**

Cross-platform container toolchain built in Nim, implementing Compose semantics and OCI-based container workflows across containerd, WSL Containers, BuildKit, and other mature runtime infrastructure.

NAVISOMA is an umbrella project for a cross-platform container workflow on Linux, WSL, macOS, and Windows with WSL Containers. Docker is not the architectural center. Compose and OCI provide portable semantics and vocabulary, while runtime/build/platform work is delegated to mature systems wherever possible.

Nim is selected deliberately for its C/C++ interoperability. NAVISOMA does **not** aim to rewrite the cloud-native ecosystem in Nim. Stable C APIs, mature C++ libraries, generated bindings/code, and thin shims are first-class implementation techniques. New Nim implementations are created only where NAVISOMA-specific semantics or a demonstrated ecosystem gap requires them.

## What NAVISOMA owns

- Compose-specific semantic processing and canonical project representation.
- A runtime-neutral canonical application model.
- Execution graph, planning, dependency scheduling and reconciliation.
- Capability-aware backend selection and diagnostics.
- Thin backend lowering/facades for containerd, BuildKit, and WSL Containers.
- One CLI/user lifecycle across supported platforms.
- Cross-backend conformance, integration, performance and explainability.

## What NAVISOMA reuses

Where suitable, NAVISOMA prefers established implementations for:

- YAML and JSON Schema;
- protobuf and gRPC;
- HTTP/2 and TLS;
- containerd runtime services;
- BuildKit solving and LLB execution;
- OCI runtime/networking infrastructure such as CNI and runc/crun-class components;
- macOS Linux VM infrastructure such as Lima;
- WSL Container native APIs on Windows.

The preferred integration order is:

```text
stable C ABI
  -> thin C/C++ shim
  -> narrow importcpp
  -> generated bindings/code
  -> existing Nim library
  -> new Nim implementation only when required
```

## Architecture

```text
compose.yaml
    |
    v
Compose semantic frontend                 NAVISOMA-owned
    |
Canonical application model               NAVISOMA-owned
    |
Execution graph / planner                  NAVISOMA-owned
    |
Runtime / build lowering                    NAVISOMA-owned
    |
Thin Nim facade / C/C++ binding            integration boundary
    |
    +--------------------------+--------------------------+
    |                          |                          |
containerd                  BuildKit                    WSLC
    |                          |                          |
Linux / WSL / macOS VM      image build               Windows
```

## Repository strategy

NAVISOMA is expected to span multiple repositories only where independently useful code is demonstrated. The `nvsm-` prefix marks NAVISOMA-maintained reusable packages without implying official upstream ownership.

Current classification:

- **strong candidate:** `nvsm-compose-nim` — Compose semantic implementation and canonical model;
- **strong/conditional:** `nvsm-wslc-client-nim` — safe Nim binding/client over WSL Container API;
- **conditional:** `nvsm-containerd-client-nim`, `nvsm-buildkit-client-nim` — only if their generated/native bridge and facade are independently reusable;
- **conditional after reuse analysis:** `nvsm-oci-spec-nim`, `nvsm-oci-client-nim`;
- **not planned by default:** NAVISOMA-specific gRPC, protobuf, JSON Schema, HTTP/2, TLS, or YAML implementations.

A separate repository is not created merely to make the ecosystem graph symmetrical.

## Design principles

### Reuse before reimplementation

Nim's `importc` and `importcpp` are architectural tools, not escape hatches. Mature native code should be reused when it provides the required semantics and a maintainable compatibility boundary.

### Specifications before vendor APIs

Compose and OCI define the portable model. Docker compatibility may be useful, but Docker Engine is not the internal abstraction.

### One user model, multiple implementations

WSL Containers are not a special side workflow. Linux, WSL, macOS and Windows expose the same NAVISOMA operations; platform differences stay below the canonical model.

### Explicit execution graph

Compose lifecycle requirements become explicit actions such as resolve/pull/build, create network/volume/container, start, wait for health, exec, stop, and remove. This supports scheduling, deterministic teardown, dry-run/explain, and reconciliation.

### Capability-aware runtimes

Networking, storage, execution, resource, GPU, image and build capabilities are explicit rather than assumed identical across backends.

### Conformance first

Compatibility claims require specification/reference/differential tests and real backend execution. Passing mocked tests is not enough, and native FFI boundaries must be tested for ownership, cleanup, errors, ABI/version compatibility and real I/O.

## Research and planning

- [`docs/project-vision.md`](docs/project-vision.md) — revised scope, ownership, goals, and non-goals.
- [`docs/implementation-plan.md`](docs/implementation-plan.md) — workstreams and dependency order under the native-reuse architecture.
- [`docs/ecosystem-research.md`](docs/ecosystem-research.md) — revised build-vs-reuse conclusions across the container ecosystem.
- [`docs/reference-implementations.md`](docs/reference-implementations.md) — codebases to reuse/study, compatibility oracles, and provenance policy.
- [`docs/native-library-reuse.md`](docs/native-library-reuse.md) — detailed C/C++ reuse research and FFI strategy.
- [`docs/c-cpp-integration-assets.md`](docs/c-cpp-integration-assets.md) — concrete per-component use of WSLC SDK, gRPC/protobuf C++, containerd/BuildKit generated clients, JSON Schema C++, and platform-native assets.
- [`docs/architecture.md`](docs/architecture.md) — semantic core, native integration boundaries, backends, and testing.
- [`docs/repository-strategy.md`](docs/repository-strategy.md) — extraction criteria and reduced repository plan.

## Status

NAVISOMA is in research, architecture, and interoperability validation. Current implementation decisions are driven by compatibility evidence, native API stability, packaging/reproducibility, conformance, maintenance cost, and real workloads.

## License

See [`LICENSE`](LICENSE).
