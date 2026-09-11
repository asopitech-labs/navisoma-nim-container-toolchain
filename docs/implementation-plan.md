# NAVISOMA Implementation Plan

## Planning premise

NAVISOMA uses Nim to **integrate** mature C/C++ and system infrastructure, not to replace it. The project plan therefore separates work into:

- semantics NAVISOMA must own;
- native integration boundaries NAVISOMA must maintain;
- upstream systems NAVISOMA should reuse;
- conformance/quality infrastructure that proves the composition is correct.

This document defines dependency order and workstreams, not an MVP cut or a schedule.

The concrete native implementation paths are defined in [`native-c-cpp-integration-plan.md`](native-c-cpp-integration-plan.md). Production implementation should follow that document rather than reopening library/protocol choices inside individual backend tasks.

## Workstream A — Native interoperability foundation

Establish the project-wide rules for using C/C++ safely from Nim.

Required outcomes:

- decision matrix for `importc`, `importcpp`, thin shims and generated code;
- C versus C++ Nim backend policy by subsystem;
- ownership/lifetime conventions;
- callback/thread-entry rules;
- error conversion rules;
- allocator boundaries;
- exception/RTTI policy for C++ integrations;
- dynamic/static linking policy;
- native dependency version/provenance manifest;
- reproducible acquisition/build/cache strategy across Linux, WSL, macOS and Windows.

Locked default directions are now:

- keep the NAVISOMA core behind C-oriented ABI boundaries;
- use direct C binding for WSLC;
- use official protobuf C++ + gRPC C++ generated code behind project-owned `extern "C"` facades for containerd and BuildKit;
- use NimYAML for YAML;
- use existing C++ JSON Schema validation behind a small C facade;
- do not implement protocol transports, CNI, OCI low-level runtimes or VM technology.

This work is tracked primarily by Issues #2, #11 and #12.

## Workstream B — Compose semantics

Compose is a NAVISOMA-owned semantic layer.

Reuse:

- existing YAML implementation;
- existing JSON Schema implementation;
- Compose schema/specification/reference fixtures.

Own:

- interpolation semantics;
- merge processing;
- include/extends handling;
- defaults and normalization;
- path rules;
- consistency checks;
- canonical Compose project representation;
- strict/default/loose compatibility behavior;
- differential tests against reference implementations.

Primary issue: #1.

Potential extracted repository: `nvsm-compose-nim` after its independent API is proven.

## Workstream C — Canonical model and execution graph

Define the product core that remains independent of all native backends.

Own:

- canonical service/application model;
- desired-state identity;
- action graph;
- dependency and health semantics;
- deterministic teardown;
- reconciliation decisions;
- dry-run/explain representation;
- capability requirements;
- product-level error taxonomy.

No containerd, WSLC, BuildKit, gRPC or protobuf types cross this boundary.

Primary issue: #3.

## Workstream D — containerd integration

Reuse:

- containerd itself;
- upstream protobuf service definitions;
- official protobuf/gRPC C++ runtimes;
- generated C++ message/stub code;
- CNI/low-level runtime infrastructure reached through the container stack.

Study:

- official containerd client behavior;
- nerdctl service sequencing and cleanup behavior.

Own:

- a narrow project-owned C ABI facade over the generated C++ client;
- the smallest Nim facade required by NAVISOMA;
- namespace/context propagation;
- operation composition needed by execution actions;
- error/capability normalization;
- lifecycle/recovery integration with the planner.

Primary issue: #4.

Standalone repository is conditional.

## Workstream E — WSL Containers integration

Reuse the WSL Container API through its native C projection.

Preferred path:

```text
pinned Microsoft.WSL.Containers SDK
  -> wslcsdk.h generated raw bindings
  -> importc
  -> safe Nim handles/lifecycle/error/capability facade
```

A C++/WinRT layer is not required for the initial backend.

Own:

- generated/raw binding maintenance;
- safe Nim handle/lifetime wrappers;
- errors;
- capability normalization;
- lowering from common execution actions.

Primary issue: #5.

`nvsm-wslc-client-nim` is a strong extraction candidate if independently useful.

## Workstream F — BuildKit integration

Reuse:

- BuildKit solver;
- LLB/protobuf definitions;
- official protobuf/gRPC C++ runtimes and generated code.

Own:

- a narrow C ABI client/event facade over generated BuildKit C++ stubs;
- Compose `build:` to build-action lowering;
- Build backend abstraction;
- cache/result identity as exposed to NAVISOMA;
- progress/event translation;
- handoff of resulting OCI artifacts/images into runtime workflows.

Primary issue: #7.

Standalone client repository is conditional.

## Workstream G — OCI and registry vocabulary

OCI is a semantic vocabulary and interoperability contract, not automatically a new implementation project.

First reuse/study mature OCI projects for descriptors, manifests, digests, platforms, distribution behavior, auth, retries and referrers.

NAVISOMA should own only the minimal OCI-facing data model or facade required by its canonical model and integrations.

Primary issue: #6.

`nvsm-oci-spec-nim` / `nvsm-oci-client-nim` remain conditional and should not be created until owned functionality is demonstrated.

## Workstream H — Platform provisioning

### Linux / WSL

Reuse containerd, CNI and existing low-level runtime infrastructure.

### macOS

Reuse a mature VM layer such as Lima. NAVISOMA manages the lifecycle/integration boundary but does not implement virtualization, filesystem sharing or VM networking.

### Windows

Use WSLC programmatic C APIs.

Own the common backend selection and capability model, not duplicate platform infrastructure.

Primary issue: #8.

## Workstream I — Conformance and quality

Testing spans both NAVISOMA-owned semantics and reused native boundaries.

Required classes:

- Compose differential tests;
- real-world Compose corpus;
- native ABI/ownership/error/lifecycle tests;
- generated protobuf/gRPC interoperability against real endpoints;
- OCI conformance/interoperability;
- containerd integration;
- BuildKit integration;
- WSLC integration;
- equivalent cross-backend semantic scenarios;
- reconciliation/failure-recovery tests;
- CPU/memory/I/O/API-call metrics where meaningful.

Primary issue: #9.

A test is insufficient if it proves only returned values while hiding wrong, duplicated or unnecessary work.

## Workstream J — Repository extraction and governance

Repository creation follows implementation evidence.

Current classification:

```text
Strong candidate
  nvsm-compose-nim

Strong/conditional binding candidate
  nvsm-wslc-client-nim

Conditional facade candidates
  nvsm-containerd-client-nim
  nvsm-buildkit-client-nim

Conditional only after reuse analysis
  nvsm-oci-spec-nim
  nvsm-oci-client-nim

Not planned by default
  nvsm-grpc-nim
  nvsm-protobuf-nim
  nvsm-jsonschema-nim
  nvsm-http2-nim
  nvsm-tls-nim
  nvsm-yaml-nim
```

Primary issue: #10.

## Dependency order

The workstreams are related as follows:

```text
Native interoperability policy (#2/#11/#12)
            |
            +------------------------------+
            |                              |
Compose semantics (#1)              Backend integrations
            |                       containerd (#4)
            |                       WSLC (#5)
            |                       BuildKit (#7)
            |                       OCI (#6)
            |                              |
            +--------> Canonical model / execution graph (#3)
                                      |
                         capability/platform model (#8)
                                      |
                          integrated toolchain behavior
                                      |
                   conformance / integration / metrics (#9)
                                      |
                         repository extraction (#10)
```

The architecture/library choices for native foundations are already documented. The remaining pre-production work in #11 is bounded validation of those selected paths, not a fresh technology survey.

## Code ownership test

Before adding a substantial subsystem, answer:

1. Is this behavior NAVISOMA-specific semantics?
2. Does a mature C/C++ or Nim implementation already exist?
3. Can it be reached with a stable C ABI?
4. Can a small shim provide a cleaner stable boundary?
5. Can upstream schemas generate the code?
6. Is there an independently maintained library that should be contributed to instead?
7. If a new Nim implementation is still proposed, what measured incompatibility or maintenance problem makes reuse inadequate?

If question 7 has no concrete answer, the new implementation should not be started.
