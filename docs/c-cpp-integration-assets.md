# C/C++ Asset Integration Plan

_Last verified: 2026-09-11_

## Purpose

NAVISOMA uses Nim specifically because mature C and C++ infrastructure can be reused directly instead of being reimplemented behind a language ecosystem boundary.

This document turns that principle into a concrete integration plan. For each major dependency it defines:

- which existing C/C++ asset should be used;
- whether Nim should use `importc`, `importcpp`, generated C++ code, or a thin shim;
- what NAVISOMA should own versus delegate;
- build/link/runtime implications;
- ownership, threading and ABI risks;
- what must be proved before implementation expands.

The default rule is:

```text
stable C ABI
  -> direct importc
  -> thin C shim around C++
  -> narrow importcpp
  -> generated C++ code + shim
  -> existing Nim package
  -> new Nim implementation only if the above are inadequate
```

The ordering is intentional. `importcpp` is powerful, but exposing large template-heavy C++ surfaces directly to Nim couples NAVISOMA to upstream C++ source/ABI details. A tiny stable C-shaped shim is often the better long-term boundary.

---

## 1. Nim interoperability mechanisms

### 1.1 `importc`

Use `importc` for stable C ABI functions, opaque handles, enums, POD structs, callbacks and exported globals.

Preferred use cases:

- WSL Container SDK C API;
- gRPC Core C API when using its low-level surface directly;
- operating-system APIs;
- libraries that already expose a stable C facade.

Typical pattern:

```nim
{.pragma: wslc, importc, cdecl, header: "wslcsdk.h".}

type
  WslcSession = pointer
  HRESULT = int32

proc WslcGetVersion(...): HRESULT {.wslc.}
```

Exact types and signatures must be generated or transcribed from the pinned upstream header; the example above is illustrative only.

### 1.2 `importcpp`

Nim supports importing C++ symbols and emits C++ calling syntax when compiled with the C++ backend. Use it only for narrow APIs whose types are straightforward and whose source-level compatibility is acceptable.

Good candidates:

- small header-only C++ libraries;
- immutable value objects;
- narrow wrappers whose constructors/destructors map cleanly;
- local NAVISOMA shim classes specifically designed to be called from Nim.

Avoid exposing directly:

- large STL-heavy public surfaces;
- deep template graphs;
- exception-rich APIs;
- ownership based on nested `shared_ptr`/`unique_ptr` graphs;
- APIs whose C++ ABI changes frequently.

Reference: [Nim manual — importcpp](https://nim-lang.org/docs/manual.html).

### 1.3 thin native shim

A C or simple C++ shim should be the standard technique when the upstream library is useful but its direct C++ surface is too complex.

The shim should:

- export `extern "C"` functions;
- expose opaque handles rather than C++ classes;
- translate exceptions to explicit error/status results;
- keep STL types behind the boundary;
- define explicit allocation/free pairs;
- expose callbacks with documented thread rules;
- avoid leaking upstream generated types into the NAVISOMA semantic core.

Example structure:

```text
Nim
  |
  | importc
  v
navisoma_native_bridge.h
  |
  v
bridge.cc
  |
  +-- gRPC C++
  +-- protobuf C++ generated code
  +-- jsoncons
```

This is the preferred pattern for containerd and BuildKit if direct gRPC Core usage becomes unnecessarily low-level.

### 1.4 generated bindings/code

Generated source is preferable to hand-maintaining large protocol declarations.

Potential generators:

- `protoc --cpp_out` for protobuf messages;
- `grpc_cpp_plugin` for gRPC C++ stubs;
- Nimterop or equivalent tooling for C headers when the generated output is reviewable and stable;
- project-specific codegen that emits narrow Nim declarations from pinned headers/proto descriptors.

Generated code is not automatically part of NAVISOMA's public API. The public API should remain a stable Nim facade.

---

## 2. WSL Containers: use the official WSLC SDK C API directly

### Existing asset

Microsoft's current WSLC SDK provides native artifacts including:

- `wslcsdk.h`
- `wslcsdk.lib`
- `wslcsdk.dll`

Microsoft's own MXC project documents and uses the SDK from Rust through hand-written `extern "C"` FFI. That is almost exactly the integration pattern NAVISOMA needs for Nim.

Reference implementation: [microsoft/mxc WSLC support plan](https://github.com/microsoft/mxc/blob/main/docs/wsl/wsl-container-support-plan.md).

The documented native API includes operations such as:

- `WslcGetMissingComponents`
- `WslcGetVersion`
- `WslcInitSessionSettings`
- `WslcCreateSession`
- `WslcSetSessionSettingsCpuCount`
- `WslcSetSessionSettingsMemory`
- `WslcListSessionImages`
- `WslcPullSessionImage`
- `WslcInitContainerSettings`
- `WslcSetContainerSettingsVolumes`
- `WslcSetContainerSettingsNetworkingMode`
- `WslcSetContainerSettingsPortMappings`
- `WslcSetContainerSettingsInitProcess`
- `WslcCreateContainer`
- `WslcStartContainer`
- `WslcGetContainerInitProcess`
- `WslcGetProcessIOHandle`
- `WslcGetProcessExitEvent`
- `WslcGetProcessExitCode`
- stop/delete/release operations for containers, processes and sessions.

The exact supported set is version-dependent and must be derived from the pinned SDK header, not copied permanently from this list.

### NAVISOMA integration choice

**Primary: direct `importc`.**

```text
NAVISOMA Nim backend
      |
      | importc
      v
wslcsdk.h / wslcsdk.lib
      |
      v
wslcsdk.dll
      |
      v
WSL Container service
```

Do not use C++/WinRT merely because Microsoft also ships a C++ projection. The C ABI is simpler and matches Nim's strengths.

### Runtime loading

MXC loads `wslcsdk.dll` dynamically. NAVISOMA should evaluate the same strategy rather than hard-linking it into every Windows executable.

Benefits:

- clean detection of unavailable/older WSLC installations;
- version/capability gating;
- no process-start failure solely because an optional backend DLL is absent;
- easier coexistence with preview SDK versions.

Nim can use `dynlib`/runtime symbol loading or a tiny loader shim. The chosen design must validate all required symbols before constructing a WSLC backend.

### Win32 handles and I/O

WSLC returns native Win32 handles for process I/O and exit notification. NAVISOMA should use Windows APIs directly for:

- `ReadFile` / `WriteFile` or appropriate overlapped equivalents;
- event waiting;
- handle cleanup;
- error translation.

Do not wrap these operations through another general-purpose runtime if direct Win32 calls provide the cleanest ownership model.

### Known compatibility concerns

MXC documents real differences between header-declared and runtime-supported behavior; for example UDP port mapping was declared but returned `E_NOTIMPL` in the SDK version it tested. Therefore NAVISOMA must have runtime capability probes/version gates, not compile-time assumptions.

The Windows backend should expose:

```text
raw WSLC C API
    -> safe Nim handles
    -> capability-normalized WSLC facade
    -> NAVISOMA runtime lowering
```

### NAVISOMA-owned code

Own only:

- raw binding declarations/codegen;
- safe lifetime wrappers;
- dynamic-loader/version checks;
- HRESULT/error normalization;
- capability detection;
- mapping between NAVISOMA container semantics and WSLC settings;
- asynchronous/process I/O integration.

Do **not** implement a container runtime, image engine, network stack or volume engine on Windows.

---

## 3. containerd: generated protobuf/gRPC C++ + narrow bridge

### Existing assets

containerd explicitly defines its primary product as its versioned gRPC API and maintains protobuf compatibility guarantees across non-major releases. Its API definitions are published as `.proto` files under the repository's `api/` tree.

References:

- [containerd release/API compatibility policy](https://github.com/containerd/containerd/blob/main/RELEASES.md)
- [containerd protobuf API](https://github.com/containerd/containerd/tree/main/api)
- [containerd operations/configuration](https://github.com/containerd/containerd/blob/main/docs/ops.md)

A typical local containerd endpoint is:

```text
unix:///run/containerd/containerd.sock
```

The gRPC resolver syntax officially supports Unix domain sockets, including `unix:///...` targets.

Reference: [gRPC custom name resolution](https://grpc.io/docs/guides/custom-name-resolution/).

### Integration alternatives

#### Option A — gRPC C++ generated stubs behind a C shim — preferred initial architecture

```text
containerd *.proto
     |
     | protoc + grpc_cpp_plugin
     v
C++ generated messages/stubs
     |
     v
NAVISOMA containerd_bridge.cc
     |
     | extern "C"
     v
Nim importc facade
```

Why this is preferred:

- generated C++ stubs already encode service/method plumbing;
- Unix-socket gRPC transport is handled by official gRPC;
- protobuf serialization is handled by official protobuf;
- Nim does not need to model CompletionQueue internals unless useful;
- upstream proto additions can be regenerated rather than transcribed.

The C shim should expose domain operations, not individual generic gRPC calls where possible:

```text
nvsm_containerd_connect
nvsm_containerd_list_images
nvsm_containerd_pull_image
nvsm_containerd_create_container
nvsm_containerd_create_task
nvsm_containerd_exec
nvsm_containerd_stop_task
nvsm_containerd_delete_container
nvsm_containerd_subscribe_events
```

Names are illustrative; the actual API should follow the containerd research issue and preserve required namespaces/leases/content semantics.

#### Option B — gRPC Core C API directly

The official `grpc.h` states that gRPC Core is a low-level library designed to be wrapped by higher-level libraries. It is therefore technically suitable for `importc`.

Reference: [grpc/grpc include/grpc/grpc.h](https://github.com/grpc/grpc/blob/master/include/grpc/grpc.h).

However, using Core directly means NAVISOMA must own significantly more call lifecycle, completion queue, byte-buffer, metadata, channel, deadline and generated-service dispatch plumbing. That is unnecessary unless the C++ generated client proves too difficult to package or wrap.

**Decision:** keep Core as a fallback/lower-level escape hatch, not the first containerd implementation path.

### Protobuf version pinning

Protobuf C++ explicitly does **not** promise ABI stability across releases, and generated C++ code must match the runtime version. Therefore NAVISOMA must pin:

```text
protoc version == protobuf C++ runtime version
```

for generated native client code.

Reference: [Protocol Buffers cross-version runtime guarantee](https://protobuf.dev/support/cross-version-runtime-guarantee/).

This argues for building/caching gRPC + protobuf as a controlled native dependency set rather than linking to arbitrary system versions.

### ttrpc note

containerd also has `ttrpc`, a separate lightweight local RPC protocol. It is **not wire-compatible with gRPC** even though it uses protobuf definitions. Do not assume gRPC C++ can talk to ttrpc services.

References:

- [containerd/ttrpc protocol](https://github.com/containerd/ttrpc/blob/main/PROTOCOL.md)
- [containerd/ttrpc README](https://github.com/containerd/ttrpc/blob/main/README.md)

NAVISOMA should implement/bind ttrpc only if a required containerd/plugin path actually needs it.

---

## 4. BuildKit: generated protobuf/gRPC C++ + thin build facade

### Existing asset

BuildKit already owns the hard parts:

- LLB representation and solver;
- build graph execution;
- cache resolution;
- frontends such as Dockerfile processing;
- exporters/importers;
- progress/status streaming.

NAVISOMA should not implement any of these engines.

BuildKit's public APIs are protobuf/gRPC based, so the same native stack selected for containerd can be reused.

### Integration architecture

```text
BuildKit proto/LLB definitions
       |
       | protoc + grpc_cpp_plugin
       v
C++ generated client code
       |
       v
buildkit_bridge.cc
       |
       | extern "C"
       v
Nim build facade
       |
       v
NAVISOMA Build actions
```

NAVISOMA-owned code should cover:

- lowering Compose `build:` semantics into BuildKit requests;
- build context selection and upload wiring;
- secrets/SSH/session mapping;
- progress/status conversion into NAVISOMA events;
- cache/import/export option mapping;
- OCI result identity and handoff to runtime backends.

Do not expose raw generated BuildKit protobuf types above the BuildKit facade.

### Shared native dependency stack

containerd and BuildKit should use the **same pinned protobuf/gRPC native toolchain** where compatible. Maintaining separate copies would increase binary size and create protobuf runtime collision risk.

---

## 5. JSON Schema: use an existing C++ implementation behind a tiny C facade

### Candidate asset: jsoncons

Current jsoncons JSON Schema support exposes `jsoncons::jsonschema::json_schema`, whose compiled schema is documented as immutable and thread-safe. Current evaluation options default to Draft 2020-12.

References:

- [json_schema class](https://github.com/danielaparker/jsoncons/blob/master/doc/ref/jsonschema/json_schema.md)
- [evaluation options](https://github.com/danielaparker/jsoncons/blob/master/doc/ref/jsonschema/evaluation_options.md)

### Recommended usage

Do **not** map jsoncons' C++ templates and JSON object model throughout Nim.

Instead create a tiny bridge:

```text
Nim string/bytes
    |
    v
nvsm_jsonschema_validate(schema_json, document_json, options, diagnostics)
    |
    v
jsoncons C++ JSON Schema implementation
```

Possible bridge responsibilities:

- compile schema;
- validate JSON text/value;
- produce normalized diagnostics;
- select Draft 2020-12/compatibility options;
- resolve external `$ref` using a NAVISOMA callback or resource map.

This keeps JSON Schema as an implementation detail of Compose validation rather than a new NAVISOMA ecosystem project.

### Alternative

If a maintained C library with equivalent Draft 2020-12 conformance and diagnostics is found, prefer its stable C ABI over jsoncons. Selection must be based on official JSON Schema test-suite results and required `$ref` behavior, not popularity alone.

---

## 6. Protobuf: use official C++ runtime and generated code

### Existing assets

Use:

- `protoc`;
- official C++ generated messages;
- official protobuf C++ runtime.

Reference: [Protocol Buffer Basics: C++](https://protobuf.dev/getting-started/cpptutorial/).

### NAVISOMA usage

Generated C++ messages should remain below native bridge boundaries. Nim should exchange:

- NAVISOMA semantic structs translated by the shim; or
- serialized protobuf byte buffers only where that is the cleaner API.

Do not manually create Nim representations for every containerd/BuildKit protobuf message unless concrete performance/debugging/tooling requirements justify doing so.

### Version policy

Pin `protoc` and runtime together. Do not depend on the host's arbitrary protobuf C++ shared library because C++ protobuf does not promise ABI stability.

---

## 7. gRPC: prefer official C++ generated client; retain Core C API as a lower layer

### Existing assets

The gRPC repository contains a shared C++ core, a C Core API and the C++ API. The top-level C header explicitly describes GRPC Core as intended to be wrapped by higher-level libraries.

References:

- [gRPC repository](https://github.com/grpc/grpc)
- [GRPC Core C API](https://github.com/grpc/grpc/blob/master/include/grpc/grpc.h)
- [gRPC C++ generated-code tutorial](https://grpc.io/docs/languages/cpp/basics/)

### Recommended layering

```text
Nim semantic client
       |
       v
small extern-C bridge
       |
       v
generated gRPC C++ stub
       |
       v
gRPC C++ / Core
```

Use Core C API directly only when the generated C++ approach cannot express a required transport/lifecycle need cleanly.

### Async model

Do not mirror gRPC CompletionQueue directly into NAVISOMA's public async API. The bridge should own native completion primitives and post completed operations/events into a Nim-controlled queue or callback boundary with explicit thread rules.

Requirements for callbacks into Nim:

- callback thread must be documented;
- data ownership must be complete before return or copied into Nim-managed memory;
- no C++ exception may cross the ABI;
- shutdown must wait for native callbacks/completions before Nim objects are destroyed.

---

## 8. OCI image and registry work: prefer mature implementations, but choose the integration boundary carefully

There is no single obvious C/C++ library that replaces the complete ORAS / containers/image / go-containerregistry functionality with the same ecosystem maturity. Therefore NAVISOMA should not mechanically create an `nvsm-oci-client-nim`, but neither should it force all registry operations through a weak C library simply to satisfy the C/C++ reuse rule.

Preferred decision sequence:

1. identify a mature C/C++ registry/content library with required OCI conformance;
2. if unavailable, use a mature command/API boundary such as ORAS only if process/API integration is acceptable;
3. otherwise implement the limited OCI Distribution HTTP operations NAVISOMA actually needs using an existing mature native HTTP/TLS library;
4. keep content descriptors/digests and canonical OCI vocabulary in the NAVISOMA semantic model where useful.

Reference projects to study even when not linked:

- [ORAS](https://github.com/oras-project/oras)
- [go-containerregistry](https://github.com/google/go-containerregistry)
- [containers/image](https://github.com/containers/image)

This is one area where "C/C++ reuse" must not become a worse engineering constraint than using the best existing implementation.

---

## 9. Networking and low-level runtime: call existing components, do not bind/rewrite unless required

### CNI

Use existing CNI plugins/runtime integration through containerd/nerdctl-style boundaries. NAVISOMA should own desired network semantics and capability diagnostics, not implement bridge/NAT/IPAM engines.

### runc / crun

Use them through containerd. Do not create direct Nim bindings unless a future backend intentionally bypasses containerd and requires OCI Runtime Spec process control.

### RootlessKit

Reuse as a process/service where rootless operation requires it. Do not port its networking/user-namespace machinery.

---

## 10. macOS: reuse Lima as platform infrastructure

macOS requires a Linux VM for Linux containers. NAVISOMA should reuse Lima or another mature VM layer for:

- VM lifecycle;
- Linux kernel/guest provisioning;
- host/guest filesystem integration;
- networking/port forwarding primitives where appropriate.

NAVISOMA's containerd C++/gRPC bridge can communicate with containerd inside the VM once an endpoint is exposed securely.

Do not implement a hypervisor or duplicate Lima's filesystem/networking machinery.

---

## 11. Native dependency topology

The intended native dependency structure is approximately:

```text
NAVISOMA (Nim)
  |
  +-- Compose semantic engine -------------------- NimYAML
  |                                           \-- JSON Schema bridge -> jsoncons (candidate)
  |
  +-- containerd facade -> C bridge -> generated gRPC C++/protobuf
  |                                    |-- grpc++ / grpc core
  |                                    \-- protobuf C++
  |
  +-- BuildKit facade  -> C bridge -> generated gRPC C++/protobuf
  |                                    |-- same grpc++ / grpc core
  |                                    \-- same protobuf C++
  |
  +-- WSLC facade -------------------- direct importc -> wslcsdk.dll
  |
  +-- platform infrastructure
       |-- containerd / CNI / runc-class runtime on Linux/WSL/macOS VM
       |-- Lima on macOS
       \-- WSLC service on Windows
```

The key property is that gRPC/protobuf are **shared native dependencies**, not separate NAVISOMA language implementations per backend.

---

## 12. Build system implications

### Native source of truth

NAVISOMA needs a native dependency manifest recording at least:

```text
dependency
upstream version/commit
source URL
license
build options
architecture
static/dynamic mode
patches/shims
ABI/API compatibility notes
security update status
```

### CMake as native sub-build

gRPC/protobuf and small C++ bridges already have strong CMake support. NAVISOMA should evaluate using CMake as a contained native-dependency build layer invoked by Nimble/its own build orchestration rather than forcing complex C++ dependency construction into Nim compiler flags.

Suggested build split:

```text
native/
  CMakeLists.txt
  bridges/
  generated/

src/
  navisoma/...nim
```

Nim then links against a deliberately small number of native outputs, ideally one NAVISOMA bridge library plus platform-specific dependencies.

### Static versus dynamic

Initial preference:

- WSLC: dynamic `wslcsdk.dll` because it is an OS/backend capability and may be absent/versioned independently;
- gRPC/protobuf: evaluate static linking into the NAVISOMA bridge to avoid system ABI drift, subject to binary size and licensing/build-time measurements;
- header-only jsoncons: compile into the bridge;
- system runtime components (`containerd`, `buildkitd`, Lima, CNI plugins): external processes/services, not linked.

This is a research default, not a final release policy.

---

## 13. Memory, ownership and exception rules

Every native bridge must publish an ownership table.

Required rules:

1. No C++ exception crosses an `extern "C"` boundary.
2. The same allocator family that allocates native memory frees it.
3. Prefer caller-provided buffers or explicit `create/free` APIs over returning raw STL-owned storage.
4. Opaque native handles have one documented release operation.
5. Nim GC-managed memory must not be retained by C/C++ after a call unless explicitly pinned/owned by a callback object with lifecycle management.
6. Native callbacks entering Nim must document their thread and reentrancy behavior.
7. Shutdown ordering must be explicit for gRPC completion queues, callbacks, WSLC process handles and dynamic libraries.

Recommended C ABI error form:

```c
nvsm_status operation(..., nvsm_error **error);
void nvsm_error_free(nvsm_error *error);
```

or an equivalent design that prevents exceptions/HRESULT/grpc native details from leaking into the semantic core.

---

## 14. Platform plan

| Asset | Windows | Linux/WSL | macOS | Integration |
|---|---|---|---|---|
| WSLC SDK | yes | n/a | n/a | direct `importc`, dynamic DLL |
| gRPC C++/Core | supported | supported | supported | native bridge / generated stubs |
| protobuf C++ | supported | supported | supported | generated code + pinned runtime |
| jsoncons | supported C++ | supported | supported | compile into C++ shim |
| containerd | typically Linux VM/WSL side | native | Lima/VM | gRPC over Unix socket/forwarded endpoint |
| BuildKit | process/service | native | VM/service | gRPC generated client |
| Lima | n/a | n/a | native host tool | process/API integration |
| CNI/runc | Linux environment | native | inside VM | delegated through runtime |

The same NAVISOMA semantic API must sit above these differences.

---

## 15. What NAVISOMA should actually write

After concrete native reuse, the expected owned code is much smaller than a "Nim cloud-native ecosystem" rewrite:

### Definitely owned

- Compose-specific interpolation/merge/include/extends/default/normalization semantics not provided by a generic library;
- Canonical Application Model;
- Execution Graph and scheduler;
- desired/observed reconciliation;
- runtime/build capability model;
- containerd semantic facade and C++ bridge glue;
- BuildKit semantic facade and C++ bridge glue;
- WSLC binding safety/lifecycle facade;
- cross-platform backend selection/provisioning orchestration;
- diagnostics, explainability and tests.

### Generated/mostly mechanical

- protobuf C++ output for containerd/BuildKit;
- gRPC C++ client stubs;
- raw WSLC C declarations if binding generation proves reliable.

### Reused

- protobuf wire/runtime;
- gRPC HTTP/2/TLS/channel implementation;
- JSON Schema engine;
- containerd services;
- BuildKit solver;
- WSLC runtime;
- CNI/runc/rootless infrastructure;
- Lima VM stack.

### Not justified without new evidence

- `nvsm-grpc-nim` implementing gRPC itself;
- NAVISOMA protobuf runtime;
- NAVISOMA JSON Schema validator;
- NAVISOMA HTTP/2/TLS stack;
- custom low-level OCI runtime;
- custom macOS virtualization stack.

---

## 16. Required validation spikes

Before production implementation, build real minimal programs for the following paths:

1. **WSLC:** Nim `importc` -> dynamically load SDK -> version/preflight -> create session/container -> capture process output -> cleanup.
2. **gRPC/protobuf:** `.proto` -> generated C++ -> tiny extern-C client -> Nim call -> Unix-domain-socket gRPC endpoint.
3. **containerd:** Nim -> bridge -> containerd version/namespaces call over `unix:///run/containerd/containerd.sock`.
4. **BuildKit:** Nim -> bridge -> basic BuildKit status/version/solve interaction with streaming progress.
5. **JSON Schema:** Nim -> extern-C shim -> jsoncons Draft 2020-12 validation -> structured diagnostic returned to Nim.
6. **Cross-platform build:** compile/link the same bridge architecture on Windows, Linux/WSL and macOS even where the actual backend differs.

These spikes should measure binary-size impact, startup cost, FFI overhead, native dependency build time, cleanup correctness and failure behavior. A design is not accepted solely because it compiles.

---

## 17. Decision summary

| Domain | Existing asset | Concrete use | NAVISOMA ownership |
|---|---|---|---|
| WSLC | `wslcsdk.h/.lib/.dll` | direct `importc`, runtime DLL loading | safe facade, capability/lifecycle mapping |
| containerd | official `.proto` + gRPC API | generated C++ stubs behind C bridge | semantic facade/lowering |
| BuildKit | official protobuf/gRPC API + solver | generated C++ stubs behind C bridge | Compose build lowering and result/progress mapping |
| gRPC | gRPC C++ / Core C | C++ generated clients; Core fallback | almost no protocol implementation |
| protobuf | official C++ runtime/protoc | generated C++ messages | version pin/build glue only |
| JSON Schema | jsoncons candidate | header-only C++ compiled behind C shim | Compose-specific diagnostics integration |
| YAML | NimYAML candidate | direct Nim dependency | Compose semantics above parser |
| CNI/runc | existing runtime ecosystem | delegated through containerd/runtime | capability/config semantics only |
| Lima | Lima | external platform provisioning | lifecycle orchestration/config only |

The revised architecture uses Nim where Nim adds leverage: semantic modeling, orchestration, compile-time tooling, safe facades and cross-platform integration. It does not use Nim as a reason to replace mature native implementations.
