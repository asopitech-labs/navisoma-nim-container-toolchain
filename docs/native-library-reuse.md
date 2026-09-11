# Native C/C++ Library Reuse Strategy

## Why this document exists

NAVISOMA is written in Nim, but that does **not** imply that every protocol, validator, client stack, or systems component should be reimplemented in Nim.

Nim's C and C++ backends are one of its strongest practical advantages. `importc` and `importcpp` make native libraries directly callable, and Nim can generate either C or C++ as its compilation backend. Therefore the default NAVISOMA question must be:

> Is there already a mature C/C++ implementation with a suitable public ABI/API that Nim can call directly?

Only after that question fails should NAVISOMA consider a new Nim-native implementation.

This substantially reduces the amount of ecosystem code NAVISOMA actually needs to create.

The concrete, implementation-level decisions derived from this research are now authoritative in [`native-c-cpp-integration-plan.md`](native-c-cpp-integration-plan.md). This document explains the rationale and alternatives; the integration plan defines the selected path to implement.

## Revised implementation policy

For every subsystem, evaluate in this order:

1. **Direct C ABI reuse** — preferred when a stable C API exists.
2. **Direct C++ API reuse through `importcpp`** — preferred for stable C++ APIs, especially header-only libraries.
3. **Thin C/C++ shim** — use when a C++ API is template-heavy, exception-heavy, or difficult to expose cleanly to Nim.
4. **Binding/code generation** — use for large repetitive native APIs or generated protocol classes.
5. **Existing Nim implementation** — use when it already provides a better ergonomic and maintenance boundary.
6. **New Nim implementation** — only when none of the above satisfies portability, stability, licensing, conformance, maintenance, or performance requirements.

The project should not treat "native Nim" as synonymous with "implemented from scratch in Nim". A Nim API over a mature C/C++ core is still a native Nim integration.

## Nim interoperability capability

Nim officially supports C and C++ foreign-function interfaces.

### C

`importc` exposes C symbols directly. Libraries with stable C headers are the simplest integration target.

Typical NAVISOMA structure:

```text
Nim API
  |
importc
  |
stable C ABI
  |
existing native library
```

### C++

Nim's C++ backend and `importcpp` support classes, methods, namespaces, constructors/destructors and templates. This makes header-only and conventional C++ libraries viable without first creating a C ABI for every operation.

```text
Nim API
  |
importcpp
  |
C++ headers/classes/templates
```

Where direct `importcpp` creates excessive surface area or ties Nim code too tightly to unstable C++ ABI details, a small project-owned C++ shim should expose a compact API.

### Binding generation

Large C APIs should not be manually transcribed by default. Relevant existing tools include:

- Futhark, which uses Clang semantics to generate Nim bindings from C headers;
- nimterop;
- c2nim.

Futhark is particularly relevant for macro/conditional-heavy C APIs because it delegates parsing and semantic interpretation to Clang.

The binding generator is an implementation tool, not a runtime dependency.

## gRPC: do not implement a new transport stack first

The earlier assumption that NAVISOMA might require `nvsm-grpc-nim` as a complete native gRPC reimplementation is too aggressive.

The official gRPC project exposes **gRPC Core**, a low-level library explicitly designed to be wrapped by higher-level language libraries. Its public API is exposed through C headers such as `grpc/grpc.h` and security-related headers.

This is almost exactly the kind of boundary Nim is good at consuming.

### Preferred approach

```text
NAVISOMA / Nim
      |
small ergonomic Nim wrapper
      |
importc
      |
gRPC Core C API
```

This means NAVISOMA does **not** initially need to implement:

- HTTP/2 framing;
- HPACK;
- gRPC wire framing;
- TLS transport;
- completion queues;
- low-level connection management;
- metadata transport;
- status/trailer transport.

These are mature gRPC Core responsibilities.

### What NAVISOMA may still need

A higher-level Nim layer is still useful for:

- lifetime/ownership safety;
- Nim-friendly async integration;
- unary/streaming call abstractions;
- cancellation/deadlines;
- conversion between Nim byte sequences/strings and gRPC slices/buffers;
- Unix-domain socket channel configuration needed by containerd;
- diagnostics and error mapping.

This is a binding/wrapper project, not a gRPC implementation project.

### C++ alternative

The official gRPC C++ API can also be used through `importcpp`, including generic call interfaces working with byte buffers. A small C++ shim around the generic client API may provide a much smaller integration surface than wrapping all of gRPC Core manually.

After further research, the selected default for containerd/BuildKit is now **generated gRPC C++ stubs behind a project-owned C ABI facade**. gRPC Core remains a lower-level fallback. See [`native-c-cpp-integration-plan.md`](native-c-cpp-integration-plan.md).

## Protocol Buffers: reuse mature native runtimes

NAVISOMA should not implement protobuf wire encoding from scratch merely because containerd and BuildKit use protobuf.

The selected default is official Protocol Buffers C++ plus `protoc` generated C++ code, kept behind the same native facade as generated gRPC stubs. This avoids exposing protobuf classes to Nim and avoids maintaining a new protobuf runtime.

## JSON Schema: a Nim implementation is unnecessary by default

The earlier proposal to create a full JSON Schema Draft 2020-12 implementation in Nim should no longer be considered the default.

`jsoncons` is a mature C++ header-only library that includes JSON Schema support for Draft 2020-12. The selected default is a tiny project-owned C ABI facade accepting JSON/schema input and returning normalized validation diagnostics. Direct exposure of jsoncons' C++ object model to Nim is not planned.

## YAML: keep NimYAML

NimYAML already provides a language-native YAML implementation and reports passing the current YAML 1.2 test suite. There is no value in replacing it merely because C/C++ YAML libraries exist.

## Compose: this remains genuinely NAVISOMA-specific work

Compose is the strongest case where substantial new Nim code is still justified. The problem is not YAML decoding but Compose semantic processing: interpolation, environment handling, merge/include/extends, defaults, uniqueness, normalization, path resolution, consistency checks and canonical project representation.

`compose-go` remains the primary reference/oracle, while lower-level YAML and schema validation are delegated to existing implementations.

## OCI specification model

OCI structures are relatively small JSON-defined models. NAVISOMA does not need a large OCI runtime implementation merely to represent descriptors, manifests, indexes, image config, platforms and digests. Define only the small typed vocabulary needed by the canonical model, using OCI specs as the contract.

## OCI registry

A registry client is HTTP/TLS plus OCI/Docker Distribution semantics. NAVISOMA should not create an HTTP/TLS stack. Mature HTTP/TLS implementations should be reused; NAVISOMA owns auth/media negotiation/blob/digest/referrer/retry behavior only where needed.

## containerd

containerd's public service interfaces are protobuf/gRPC. The selected default path is:

```text
containerd upstream .proto
  -> protoc + grpc_cpp_plugin
  -> official protobuf/gRPC C++ generated client
  -> project-owned extern-C facade
  -> Nim importc facade
  -> NAVISOMA backend semantics
```

The official containerd Go client and nerdctl remain semantic references for sequencing, namespaces, leases, cleanup and failure behavior.

## BuildKit

The same reduction applies to BuildKit. Use upstream protobuf/LLB definitions, official generated C++ gRPC/protobuf client code, and a thin C ABI facade. NAVISOMA owns Compose-build lowering, cache/result identity, progress normalization and integration—not the BuildKit solver or RPC stack.

## WSL Containers

Microsoft exposes a WSLC SDK C ABI. This is an ideal Nim integration target. The selected path is generated/direct bindings from the exact pinned `wslcsdk.h`, `importc`, safe Nim handle/lifecycle wrappers, capability normalization and backend lowering. Microsoft MXC's generated-bindings/update runbook is the reference pattern.

## Networking, rootless execution and VM management

CNI, RootlessKit, Lima and runc/crun-class runtimes should be reused rather than reimplemented. NAVISOMA owns lifecycle coordination and semantic integration only.

## Build/distribution implications

C/C++ reuse shifts complexity from protocol implementation to build engineering. NAVISOMA must explicitly manage static/dynamic linking, package discovery, source builds, ABI/version pinning, security updates, license propagation, cross-platform binaries and reproducible build caches. Issue #12 owns this work.

## C++ ABI boundary rule

Prefer:

```text
stable C API
  > small project-owned C ABI shim over C++
  > narrow importcpp surface
  > large direct C++ object graph exposure
```

This isolates NAVISOMA from C++ ABI/source churn even though Nim can technically call C++ directly.

## What NAVISOMA should actually invent

Original code should concentrate on product value:

- Compose semantic processing;
- canonical application model;
- execution graph/planning/reconciliation;
- runtime-neutral semantics and capability model;
- backend lowering/facades;
- cross-platform provisioning coordination;
- conformance, explainability and performance measurement.

The protocol/runtime foundations beneath those layers should be reused.
