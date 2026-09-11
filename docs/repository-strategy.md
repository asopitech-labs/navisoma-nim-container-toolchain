# NAVISOMA Repository Strategy

## Principle

NAVISOMA does not create repositories to mirror every layer of the container ecosystem in Nim.

A repository is justified only when NAVISOMA owns or maintains an independently useful boundary. Thin internal glue should remain in the main repository unless it has clear external value.

The `nvsm-` prefix remains reserved for NAVISOMA-maintained reusable packages so they are identifiable without appearing to be official upstream language implementations.

## Main repository

```text
asopitech-labs/navisoma-nim-container-toolchain
```

Responsibilities:

- umbrella documentation/research;
- CLI and user workflow;
- canonical application model;
- execution graph/planner/reconciliation;
- capability model;
- backend interfaces;
- internal native shims/bindings that are not independently useful;
- cross-platform provisioning integration;
- end-to-end conformance/integration/performance tests.

## Strong repository candidates

### `nvsm-compose-nim`

This is the clearest independent implementation boundary because Compose-specific semantics are product logic NAVISOMA must understand directly.

Potential responsibilities:

- interpolation;
- merge semantics;
- include/extends processing;
- defaults and normalization;
- path handling;
- consistency validation;
- canonical Compose project model;
- integration with existing YAML and JSON Schema engines;
- reference/differential test corpus.

Generic YAML or JSON Schema parsing is not part of this package unless evidence shows no usable external implementation.

### `nvsm-wslc-client-nim`

A strong/conditional candidate because WSLC exposes a native API and a safe Nim binding may be independently useful.

Potential responsibilities:

- direct C ABI declarations or a minimal native shim;
- ownership-safe Nim handles;
- session/container/process operations;
- volumes, networking and ports;
- capability normalization;
- error translation;
- SDK/ABI version handling.

This package does not contain Compose orchestration.

## Conditional repository candidates

### `nvsm-containerd-client-nim`

Create only if the containerd generated/native bridge plus ergonomic Nim facade has independent value beyond NAVISOMA.

It should reuse upstream protobuf definitions, generated client code and mature protobuf/gRPC native runtimes rather than implement those protocols.

### `nvsm-buildkit-client-nim`

Create only if the BuildKit/LLB bridge/facade is independently reusable. BuildKit solving/caching/runtime logic remains upstream.

### `nvsm-oci-spec-nim` / `nvsm-oci-client-nim`

These are no longer assumed repositories. First determine whether NAVISOMA can use existing native/reference OCI implementations plus a smaller internal vocabulary/facade. Create standalone packages only if substantial reusable owned behavior remains.

## Not default repositories

The following are explicitly **not** planned merely because no perfect Nim package exists:

```text
nvsm-grpc-nim
nvsm-protobuf-nim
nvsm-jsonschema-nim
nvsm-http2-nim
nvsm-tls-nim
nvsm-yaml-nim
```

Existing C/C++ or Nim implementations are the first choice. New implementations require a separate evidence-backed decision.

## Binding versus facade versus semantic implementation

Every package must declare which category it belongs to.

### Binding

Mechanical exposure of an upstream C/C++ ABI/API. Binding code should be thin and version-aware.

### Shim

Small native code used to convert a difficult C++ interface into a stable, narrow C-style boundary. A shim is not an excuse to duplicate upstream logic.

### Client facade

Ergonomic Nim API over generated/native client code. It may own lifecycle/error translation but not upstream protocol/runtime semantics.

### Semantic implementation

Logic whose behavior NAVISOMA must own and test against a specification/reference, such as Compose processing.

Repository extraction is most justified for semantic implementations and externally useful client facades; least justified for trivial internal bindings.

## Native dependencies

Reusable packages may depend on native libraries. Each such repository must document:

- upstream project and license;
- supported upstream versions;
- static/dynamic linking expectations;
- package/distribution requirements;
- ABI/API compatibility policy;
- generated sources and regeneration instructions;
- security/update policy.

Do not hide a large vendored native stack behind a small Nimble package without making its provenance and build behavior explicit.

## Dependency direction

```text
existing native libraries
 protobuf / gRPC / JSON Schema / TLS / platform APIs
                 |
       generated code / bindings / shims
                 |
 optional reusable facades
 containerd / BuildKit / WSLC / OCI
                 |
        NAVISOMA-owned semantics
 Compose -> canonical model -> execution graph
                 |
                 CLI
```

A lower-level package must not depend on the NAVISOMA CLI or planner.

## Creation criteria

A standalone repository is created only when:

1. the responsibility has a coherent public API;
2. code ownership is meaningful, not only a few internal FFI declarations;
3. independent consumers are plausible or the binding is clearly valuable on its own;
4. native/library provenance and packaging are understood;
5. conformance/interoperability testing is defined;
6. repository/Nimble naming collision is checked;
7. maintaining a separate release/version lifecycle is justified.

Otherwise keep the code in the main repository.

## Versioning

Each independent package versions its own public surface and tracks relevant upstream ABI/API compatibility separately. NAVISOMA does not synchronize all package versions.

Bindings/facades should explicitly publish the supported upstream version range.

## Governance

Each extracted repository must state:

- that it is maintained under NAVISOMA;
- whether it is a binding, shim, client facade or independent semantic implementation;
- that it is unofficial unless upstream ownership says otherwise;
- upstream versions/specifications targeted;
- conformance/interoperability status;
- native dependency and license provenance.
