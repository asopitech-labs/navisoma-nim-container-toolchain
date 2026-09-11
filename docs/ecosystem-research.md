# Nim Container Ecosystem Research

## Scope

This document records the research that led to NAVISOMA's ecosystem-oriented architecture. It focuses on what already exists in Nim, which layers appear reusable, and which gaps justify dedicated implementations.

The central conclusion is that Nim is not missing every primitive required for container tooling. The larger problem is that specification-oriented infrastructure is fragmented and does not currently form an end-to-end Compose/OCI/container toolchain comparable to the ecosystems available in Go and Rust.

## Compose Specification

Compose is an open specification, not merely the private file format of Docker Compose. The Compose ecosystem maintains the Compose Specification and a Go reference/library implementation, `compose-go`.

A correct Compose implementation requires substantially more than parsing YAML into objects. Relevant processing includes:

- YAML decoding
- variable interpolation and escaping
- per-file processing order
- multi-file merge semantics
- `include`
- `extends`
- default values
- uniqueness rules
- path resolution
- normalization/canonicalization
- schema validation
- semantic/consistency validation
- capability handling for implementation-specific support

### NAVISOMA direction

A native reusable implementation is justified. The working repository name is `nvsm-compose-nim`.

The implementation should expose intermediate processing phases instead of hiding environment access and normalization behind one opaque `load()` operation. This makes the library useful for editors, linters, migration tools, static analysis, CI validation and alternate orchestrators in addition to NAVISOMA itself.

`compose-go` is an important behavioral/reference oracle, but NAVISOMA should not require a Go runtime or a Go shared-library bridge for its canonical Compose model.

## YAML

Nim already has mature YAML work. NimYAML is a pure-Nim YAML implementation with YAML 1.2 support and should be evaluated as the default parsing foundation.

### Direction

Do not create a NAVISOMA YAML parser merely for ecosystem symmetry. Compose-specific behavior belongs above a generic YAML implementation.

## JSON Schema

Compose publishes a machine-readable schema, and OCI specifications also make extensive use of schema-defined structures. Nim has packages using the term "jsonschema", but the ecosystem does not currently provide an obvious, broadly adopted, complete JSON Schema Draft 2020-12 implementation comparable to mature implementations in larger ecosystems.

### Direction

A standards-compliant JSON Schema implementation has independent ecosystem value. If existing implementations cannot satisfy the required draft/conformance coverage, this is a strong candidate for a separate reusable repository rather than code embedded in the Compose parser.

The final repository name should be chosen after a dedicated package and collision survey.

## Protobuf

Nim already has protobuf implementations. Relevant projects include `protobuf-nim` and Status ecosystem protobuf serialization work. Therefore protobuf wire encoding should not be assumed to require a new implementation.

The areas that require closer evaluation are:

- proto2/proto3/Editions coverage
- descriptor support
- `.proto` parser quality
- code generation
- unknown-field behavior
- well-known types
- compatibility tests against protoc/reference implementations
- maintenance on current Nim versions

### Direction

Reuse and strengthen an existing implementation where possible. A NAVISOMA-prefixed protobuf repository should only exist if a genuine specification/tooling gap remains after evaluation.

## gRPC

The historical claim that Nim has no meaningful gRPC implementation is no longer accurate. Joubako demonstrates native Nim-facing gRPC functionality over an HTTP/2-capable transport and supports unary and streaming RPC patterns.

This changes the problem from "implement all of gRPC from zero" to "determine whether existing implementations can become a maintained, specification-compatible foundation for container infrastructure."

Evaluation areas include:

- unary RPC
- client streaming
- server streaming
- bidirectional streaming
- metadata and trailers
- status mapping
- deadlines/cancellation
- compression
- backpressure
- authentication/TLS
- Unix-domain socket transport needed by local containerd deployments
- Windows named-pipe/other transport needs where applicable
- generated client/server APIs
- interoperability and conformance

### Direction

`nvsm-grpc-nim` is a candidate, not an automatic new repository. Prefer upstream contribution, extraction, or reuse if existing projects can meet the requirements.

## containerd

containerd exposes its primary programmatic interfaces through versioned gRPC/protobuf APIs. This means a Nim implementation does not require Go merely because containerd is implemented in Go.

A useful native client must deal with more than container creation. Relevant APIs/concepts include:

- namespaces
- images
- content store
- containers
- snapshots
- tasks/processes
- events
- leases
- streaming
- metadata and lifecycle coordination

### Direction

`nvsm-containerd-client-nim` should be a native API client built on the selected Nim protobuf/gRPC foundation. It should expose containerd concepts cleanly while NAVISOMA's higher-level runtime interface remains independent of containerd.

nerdctl is an important behavioral and architectural reference because it demonstrates how Compose and Docker-like UX can be implemented over containerd without making Docker Engine the runtime.

## OCI specifications

OCI provides the portable vocabulary needed to prevent Docker-specific concepts from becoming NAVISOMA's internal model. Important specifications/concepts include:

- Image Specification
- Runtime Specification
- Distribution Specification
- descriptors
- manifests
- image indexes
- image configuration
- platforms
- digests/content addressing
- image layout
- annotations and artifacts

Rust's `oci-spec-rs` / `oci-spec` provides a useful precedent for a language-native specification model.

### Direction

`nvsm-oci-spec-nim` should provide reusable typed specification models, serialization and validation where justified.

Registry/distribution operations should remain separable as `nvsm-oci-client-nim`, because HTTP registry behavior and static OCI data models have different responsibilities and dependency profiles.

## OCI Distribution / registry

The OCI Distribution Specification defines HTTP APIs for registry discovery, manifests, blobs, uploads, tags and referrers. This layer is valuable independently of Compose and container execution.

### Direction

Implement a reusable registry client only after evaluating existing Nim HTTP clients against requirements such as streaming, authentication flows, redirects, content verification, resumable transfers and large blobs.

## BuildKit and LLB

BuildKit separates build frontend concerns from a solver and uses LLB as a protobuf-serializable low-level build representation. This is strategically useful for NAVISOMA because image building should not be fused to a particular container runtime.

### Direction

`nvsm-buildkit-client-nim` is a candidate native BuildKit/LLB client. It should be built on shared protobuf/gRPC infrastructure and remain independent of the Compose parser.

Compose `build:` entries should lower into build actions whose implementation may be BuildKit or another compatible builder.

## WSL Containers / WSL Container API

Microsoft's WSL Containers provide a Windows-native container-facing execution path distinct from running a traditional Docker daemon inside a WSL distribution. The WSL Container API exposes programmable lifecycle operations and C/C#/C++ projections.

This is particularly attractive for Nim because C ABI integration is a language strength.

Relevant operations/capabilities include:

- sessions
- image operations
- container create/start/stop/delete
- processes and I/O
- volumes
- port mappings/networking
- resource configuration
- GPU-related capabilities where supported

The API is still evolving and capability differences between SDK and CLI behavior must be treated as an explicit compatibility concern rather than hidden.

### Direction

`nvsm-wslc-client-nim` should expose a safe Nim API over the supported WSL Container interfaces. NAVISOMA then lowers its runtime-neutral container semantics into that API.

WSLC is a first-class backend, not an exceptional adapter with a separate user workflow.

## macOS

Linux containers cannot run directly on the macOS kernel, so a Linux VM layer is required. Lima demonstrates an open and proven approach to VM lifecycle, filesystem sharing and networking for container workloads.

### Direction

NAVISOMA should initially reuse a suitable VM layer rather than implement a hypervisor. Once the VM is available, the preferred architecture is to communicate with the runtime API directly rather than shelling out through an entire foreign CLI stack for every container operation.

## Linux and WSL

Linux and WSL can share the containerd-oriented backend path where containerd is available. This reduces the number of runtime implementations while keeping the external NAVISOMA model identical to Windows/WSLC.

## Async runtime considerations

Nim has both the standard async ecosystem and alternatives such as Chronos. Infrastructure libraries should avoid forcing an async runtime when their responsibility is purely data modeling, parsing, serialization or validation.

Transport-heavy packages should make the runtime dependency explicit and should avoid unnecessarily increasing ecosystem fragmentation.

## Research conclusion

The strongest ecosystem gaps are not at the YAML syntax level. The high-value work lies in the specification and integration layers:

1. Compose semantic processing and canonical model.
2. JSON Schema conformance where existing Nim support is insufficient.
3. OCI specification models and registry operations.
4. A production-quality path from Nim protobuf/gRPC to containerd and BuildKit.
5. Native WSL Container API integration.
6. A common execution planner/runtime model that proves these components together.

NAVISOMA should reuse mature foundations, improve promising existing projects, and create new repositories only where the gap is real and independently valuable.
