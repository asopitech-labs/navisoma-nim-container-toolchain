# Native C/C++ Library Reuse Strategy

## Why this document exists

NAVISOMA is written in Nim, but that does **not** imply that every protocol, validator, client stack, or systems component should be reimplemented in Nim.

Nim's C and C++ backends are one of its strongest practical advantages. `importc` and `importcpp` make native libraries directly callable, and Nim can generate either C or C++ as its compilation backend. Therefore the default NAVISOMA question must be:

> Is there already a mature C/C++ implementation with a suitable public ABI/API that Nim can call directly?

Only after that question fails should NAVISOMA consider a new Nim-native implementation.

This substantially reduces the amount of ecosystem code NAVISOMA actually needs to create.

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

Both approaches must be prototyped before deciding the final boundary.

## Protocol Buffers: reuse mature native runtimes

NAVISOMA should not implement protobuf wire encoding from scratch merely because containerd and BuildKit use protobuf.

There are several viable native paths.

### Official C++ protobuf runtime

The official Protocol Buffers C++ implementation provides generated classes, descriptors, reflection, DynamicMessage, parsing and serialization.

With Nim's C++ backend, NAVISOMA can use generated C++ protobuf code directly or through a generated/thin shim.

Potential model:

```text
.proto
  |
protoc --cpp_out
  |
generated C++ message classes
  |
small generated bridge / importcpp layer
  |
Nim
```

This avoids implementing the protobuf wire format and Editions semantics in NAVISOMA.

### DynamicMessage / descriptors

The C++ runtime supports runtime descriptors and DynamicMessage. For containerd/BuildKit, where the schema is trusted and fixed by the upstream project, a descriptor-driven bridge can reduce generated Nim binding volume.

This requires benchmarking: reflection adds indirection and lookup overhead and may be less ergonomic than generated wrappers.

### protobuf-c

`protobuf-c` is a mature C implementation with `protoc-gen-c` and a stable C-facing runtime, making it very easy to bind from Nim.

However, feature compatibility must be checked carefully. Historical gaps such as proto3 optional support demonstrate that `protobuf-c` may lag modern official protobuf language/runtime semantics. It therefore cannot automatically be selected for current containerd/BuildKit APIs.

### upb

The official protobuf repository includes upb and even documents how to construct another language implementation on top of it using a language-specific code generator plus FFI glue.

This architecture fits Nim conceptually very well:

```text
.proto
  |
protoc + Nim generator
  |
Nim generated API
  |
thin FFI glue
  |
upb C core
```

But protobuf's own security guidance describes upb as an implementation-detail API with invariants that language runtimes must preserve. Therefore NAVISOMA should not casually bind raw upb internals without treating itself as a real protobuf language-runtime maintainer.

### Revised decision

Do not create a new protobuf runtime. First compare:

1. official protobuf C++ + generated bridge;
2. official protobuf C++ DynamicMessage bridge;
3. gRPC/protobuf C++ combined shim;
4. existing Nim protobuf implementation;
5. protobuf-c where API features permit it.

Only build a new Nim code generator/runtime layer if these approaches demonstrably fail.

## JSON Schema: a Nim implementation is probably unnecessary

The earlier proposal to create a full JSON Schema Draft 2020-12 implementation in Nim should no longer be considered the default.

`jsoncons` is a mature C++ header-only library that includes JSON Schema support for Draft 4, 6, 7, 2019-09 and 2020-12.

Because it is header-only and requires C++11, it is an unusually good match for Nim's C++ backend.

### Preferred options

Direct:

```text
Nim (C++ backend)
  |
importcpp
  |
jsoncons::jsonschema
```

or via a very thin shim:

```text
Nim
  |
C-style project shim
  |
jsoncons C++
```

The shim can accept schema/document UTF-8 JSON strings and return a structured validation result, dramatically reducing the exposed C++ surface.

### Consequence

A standalone NAVISOMA JSON Schema implementation is no longer justified unless jsoncons fails required conformance, portability, licensing, diagnostics, or binary-size criteria.

This removes one significant proposed repository from the likely ecosystem plan.

## YAML: keep NimYAML unless evidence favors a native C/C++ library

NimYAML already provides a language-native YAML implementation and has substantial YAML 1.2 coverage. There is no value in replacing it merely because libyaml/yaml-cpp exist.

This is an example where an existing Nim library is already the cleaner abstraction.

The decision remains evidence-based: parser behavior required by Compose and interoperability fixtures decide, not a general preference for C/C++.

## Compose: this remains genuinely NAVISOMA-specific work

Compose is the strongest case where substantial new Nim code is still justified.

The problem is not YAML decoding. It is Compose semantic processing:

- interpolation;
- environment handling;
- merge rules;
- include;
- extends;
- defaults;
- uniqueness rules;
- normalization;
- path resolution;
- consistency checks;
- canonical project representation.

The most mature reusable implementation, `compose-go`, is written in Go. There is no equivalent stable C/C++ Compose library that can simply be imported through Nim's normal FFI.

### Possible reuse strategies

#### 1. Reimplement semantics in Nim using compose-go as oracle

This remains the preferred long-term architecture if NAVISOMA wants a true Nim Compose library.

Existing native libraries can still eliminate lower-level work:

```text
NimYAML              -> YAML
jsoncons              -> JSON Schema
native filesystem/env -> paths/environment
Nim code              -> Compose-specific semantics only
```

#### 2. Wrap compose-go as a C ABI component

Go can build a `c-shared` library with exported C functions. In theory NAVISOMA could expose canonical Compose JSON from a tiny Go bridge.

Advantages:

- immediate high compatibility;
- dramatically less Compose implementation work.

Disadvantages:

- embeds Go runtime into the NAVISOMA process;
- complicates cross-compilation/distribution;
- reduces the value of a reusable Nim Compose library;
- makes Compose behavior dependent on an opaque foreign runtime.

This is worth considering as a **bootstrap/reference backend**, but not automatically as the final architecture.

## OCI specification model: much smaller than previously assumed

OCI data structures themselves are relatively small JSON-defined models. NAVISOMA does not need a large OCI runtime implementation merely to represent descriptors, manifests, indexes, image config, platforms and digests.

Options include:

- define the small typed model in Nim;
- generate model declarations from schemas/specification sources;
- use a C/C++ OCI library only if one provides meaningful additional behavior.

There is no need to create a broad `oci-spec` ecosystem clone simply for parity with Go/Rust.

The key reusable logic is content addressing, digest verification, registry transfer, media-type handling and graph traversal.

## OCI registry: prefer mature HTTP/TLS and native crypto libraries

A registry client is an HTTP client plus OCI/Docker Distribution semantics. NAVISOMA should not create an HTTP/TLS stack.

Possible native foundations include libcurl and other mature C/C++ HTTP clients. The remaining NAVISOMA code is primarily:

- authentication/challenge flows;
- manifest/media-type negotiation;
- blob transfer;
- resumable uploads;
- digest verification;
- tags/referrers;
- retry policy;
- OCI/Docker compatibility behavior.

Reference implementations such as ORAS, `go-containerregistry`, and `containers/image` should define behavior and edge cases even if their Go code is not linked directly.

## containerd: the server has no comparable stable C client library

Nim's C/C++ interoperability does not automatically solve containerd integration because containerd's normal client ecosystem is Go and its public service interfaces are protobuf/gRPC.

However, once NAVISOMA reuses gRPC Core and an existing protobuf runtime, the amount that must be created is much smaller:

```text
containerd .proto APIs
       |
existing protobuf runtime/codegen
       |
existing gRPC Core/C++ transport
       |
NAVISOMA-specific generated client facade
       |
Nim ergonomic API
```

What NAVISOMA actually owns is primarily:

- generated/API-specific wrappers;
- namespace metadata handling;
- high-level operation composition;
- lifecycle/error mapping;
- conversion into NAVISOMA's runtime-neutral semantics.

It should not own HTTP/2, protobuf encoding, TLS, or a generic gRPC implementation.

The existing containerd Go client remains the primary semantic/code reference for operation sequencing and metadata behavior.

## BuildKit: same reduction applies

BuildKit APIs and LLB are protobuf/gRPC based. Reusing native protobuf and gRPC libraries eliminates most protocol-stack work.

NAVISOMA needs:

- API/codegen facade;
- LLB model/construction ergonomics where useful;
- solve/session/progress mapping;
- Compose build lowering;
- result/cache identity handling.

It does **not** need a BuildKit solver or its own RPC stack.

## WSL Containers: C API is an ideal Nim integration target

Where Microsoft exposes a supported C projection, NAVISOMA should bind it directly with `importc` or generated C bindings.

This is likely one of the lowest-cost integrations in the project:

```text
Nim safe API
  |
importc/generated bindings
  |
Microsoft WSL Container C API
```

NAVISOMA's work is ownership/lifetime safety, capability normalization, error mapping and lowering from the common runtime model—not reimplementing WSLC.

The current Microsoft documentation prominently documents C# and C++/WinRT, with the C++ projection still preview. Therefore the exact supported C projection, headers, binary ABI and distribution model must still be validated before binding architecture is frozen.

## Networking, rootless execution and VM management

These areas should not be reimplemented.

### CNI

Use existing CNI plugins/interfaces where the backend architecture requires them. Implementing bridge/DNS/IPAM/network namespace manipulation in Nim creates risk with little strategic value.

### RootlessKit

Reuse or invoke mature rootless/user-namespace tooling instead of recreating it.

### Lima

On macOS, reuse a mature Linux VM manager such as Lima. NAVISOMA should own lifecycle coordination and runtime connection, not virtualization technology.

### runc/crun

These remain lower-level OCI runtime implementations. NAVISOMA should normally reach them through containerd or another runtime manager rather than creating a new OCI runtime.

## Revised likely repository set

The earlier multi-repository plan was too expansive.

After accounting for direct C/C++ reuse, the likely independently valuable repositories shrink to something closer to:

### Strong candidates

- `nvsm-compose-nim` — Compose-specific semantics remain substantial and lack a C/C++ reusable implementation.
- `nvsm-wslc-client-nim` — thin, reusable binding/client over the WSL Container API.
- `nvsm-containerd-client-nim` — only if a reusable generated facade over native gRPC/protobuf is cleaner than keeping it inside NAVISOMA.
- `nvsm-buildkit-client-nim` — same criterion as containerd.

### Conditional candidates

- `nvsm-oci-client-nim` — useful if registry semantics become substantial enough to justify an independent client.
- `nvsm-oci-spec-nim` — only if the typed model is sufficiently reusable; it may simply be a small package/module.

### No longer default candidates

- `nvsm-grpc-nim` — use gRPC Core/C++ or existing Nim work first.
- a NAVISOMA JSON Schema implementation — use jsoncons first.
- a NAVISOMA protobuf runtime — use official C++/C implementations or existing Nim runtime first.
- YAML/HTTP/TLS implementations — reuse mature existing libraries.

## Build/distribution implications

C/C++ reuse shifts complexity from protocol implementation to build engineering. This is acceptable, but must be treated explicitly.

NAVISOMA needs a dependency strategy covering:

- static vs dynamic linking;
- package/system-library discovery;
- vendored source builds;
- CMake/pkg-config integration;
- Windows/macOS/Linux binary availability;
- ABI/version pinning;
- security updates;
- license/NOTICE propagation;
- cross-compilation;
- reproducible builds.

The project must not hide this cost. A C++ library that is impossible to package reliably on all target platforms may still be worse than a modest Nim implementation.

## C++ ABI boundary rule

Prefer this order:

```text
stable C API
  > small project-owned C ABI shim over C++
  > narrow importcpp surface
  > large direct C++ object graph exposure
```

A thin C shim often provides the best long-term ABI isolation even when Nim can technically import a large C++ API directly.

Header-only libraries such as jsoncons are an exception because there is no separate binary ABI to track; direct `importcpp` becomes more attractive.

## What NAVISOMA should actually invent

The project should concentrate original code where its product value exists:

- Compose semantic processing;
- canonical application model;
- execution graph;
- runtime-neutral container semantics;
- capability model;
- desired/observed-state reconciliation;
- backend lowering into containerd and WSLC;
- builder lowering into BuildKit;
- cross-platform platform selection/provisioning;
- diagnostics, explainability and metrics;
- differential/conformance testing.

Everything below those layers should be presumed reusable until proven otherwise.

## Research actions before implementation

For each native dependency candidate, prototype a minimal vertical slice and measure:

1. build complexity on Linux, WSL, macOS and Windows;
2. static/dynamic binary size;
3. FFI call ergonomics and ownership safety;
4. async/callback integration;
5. API/ABI stability;
6. upstream release/security cadence;
7. cross-compilation viability;
8. license compatibility;
9. runtime performance/resource overhead;
10. whether a thin shim remains genuinely thin.

A dependency should be rejected for concrete evidence, not because it was written in C or C++.

## Updated conclusion

Nim's interoperability substantially changes NAVISOMA's scope.

The project does **not** need to build an entire cloud-native ecosystem from first principles. Its strongest architecture is likely a Nim orchestration and semantic layer sitting on mature native infrastructure:

```text
                       NAVISOMA / Nim
                             |
       +---------------------+---------------------+
       |                     |                     |
 Compose semantics     execution/planning     safe Nim APIs
       |                     |                     |
 NimYAML/jsoncons      container-neutral IR      bindings
                                                   |
                            +----------------------+
                            |
              +-------------+--------------+
              |             |              |
         gRPC Core      protobuf C++      WSLC C API
              |             |              |
         containerd / BuildKit          WSL Containers

Other native dependencies:
libcurl/HTTP/TLS, CNI, Lima, runc/crun and platform libraries
```

The revised principle is:

> **Use Nim to unify, model, plan, and expose the system. Reuse mature C/C++ implementations for protocol and systems machinery whenever their public boundary is suitable.**
