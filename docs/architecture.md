# NAVISOMA Architecture

## Architectural thesis

NAVISOMA is a specification-driven execution system written in Nim. Nim is used as the orchestration and semantic integration language because it can directly consume mature C/C++ implementations.

The architecture is therefore split between **NAVISOMA-owned semantics** and **reused native infrastructure**.

```text
Compose source
    -> Compose semantic processing            [NAVISOMA-owned]
    -> canonical application model            [NAVISOMA-owned]
    -> execution graph / planner              [NAVISOMA-owned]
    -> runtime/build lowering                 [NAVISOMA-owned]
    -> thin Nim facade / C/C++ binding        [owned integration]
    -> mature native implementation           [reused]
```

The objective is to minimize duplicated protocol/runtime code while preserving a clean, runtime-neutral product model.

## 1. Compose frontend

Inputs may include Compose YAML, environment and `.env` data, included/extended documents, build contexts, and referenced images.

Suggested processing stages:

```text
Raw YAML
   |
YAML document                  <- existing YAML library
   |
Compose interpolation
   |
merge/include/extends
   |
schema validation              <- existing JSON Schema implementation where suitable
   |
defaults / normalization
   |
path resolution
   |
semantic consistency checks
   |
Canonical Compose Project
```

Compose-specific ordering and semantics are NAVISOMA-owned and tested against the specification/reference behavior. Generic YAML and JSON Schema engines should be reused.

## 2. Canonical application model

The model represents desired application state without leaking Docker, containerd, WSLC, gRPC, protobuf, BuildKit, or native-library types.

Representative concepts:

- project and service;
- image/build source;
- command/entrypoint/environment;
- mount and volume;
- network attachment and published port;
- health/dependency condition;
- resource request/limit;
- secret/config;
- platform/image identity.

It should support stable diagnostic serialization for differential tests and explain tooling.

## 3. Execution graph

The planner lowers desired application state into explicit actions, for example:

```text
ResolveImage
PullImage
BuildImage
CreateNetwork
CreateVolume
CreateContainer
StartContainer
AwaitHealth
ExecProcess
StopContainer
RemoveContainer
RemoveVolume
RemoveNetwork
```

The graph provides dependency scheduling, parallelism, deterministic teardown, dry-run/explain output, reconciliation, failure propagation and action-level metrics.

## 4. Runtime-neutral semantics

NAVISOMA defines a small semantic backend interface rather than mirroring any vendor API.

Representative operations:

```text
Resolve/Pull image
Create/Start/Stop/Remove container
Exec process
Inspect state
Stream logs/I/O
Create/Remove network
Create/Remove volume
Query capabilities
```

Backend-native handles and protocol types terminate behind the facade.

## 5. Native integration boundary

Every backend or generic facility is integrated through the smallest practical boundary.

Preferred order:

```text
stable C ABI
  -> thin C/C++ shim
  -> narrow importcpp
  -> generated code/bindings
  -> existing Nim library
  -> new Nim implementation only if required
```

The boundary must define:

- ownership and lifetime;
- error conversion;
- callbacks and threading;
- allocator boundaries;
- async/completion behavior;
- ABI/API version expectations;
- static/dynamic linking policy;
- packaging and redistribution requirements.

## 6. containerd backend

Targets Linux, WSL and the Linux VM used on macOS.

containerd remains the runtime implementation. NAVISOMA should not recreate containerd services or a generic gRPC stack.

Preferred architecture:

```text
Execution Graph
   -> containerd facade in Nim
   -> generated containerd protobuf/gRPC client code
   -> official/mature protobuf + gRPC native runtime
   -> containerd
```

nerdctl and the official containerd Go client are code/behavior references for service sequencing, namespace propagation, image/content/task lifecycle, leases, streaming and cleanup.

## 7. WSL Containers backend

Windows uses the same canonical/execution model and lowers it to the WSL Container API.

Preferred architecture:

```text
Execution Graph
   -> WSLC facade in Nim
   -> direct C API binding where stable
      or very thin C/C++ shim where required
   -> WSL Container API
```

WSLC is a first-class backend, not a separate orchestration product.

## 8. Build backend

Image build and container execution are separate responsibilities.

```text
Build action
   -> Build facade
   -> generated BuildKit/LLB protocol code
   -> reused protobuf/gRPC native runtime
   -> BuildKit solver
```

NAVISOMA owns Compose-to-build-action lowering and planner integration. BuildKit owns solving, caching and low-level execution.

## 9. OCI

OCI defines portable vocabulary for images, descriptors, manifests, indexes, digests, platforms and distribution behavior.

NAVISOMA should expose only the OCI concepts required by its canonical model and backends. Existing OCI libraries/reference implementations should be reused or studied before introducing standalone Nim implementations.

## 10. Platform provisioning

### Linux / WSL

Use existing runtime/network/storage components. Do not recreate CNI, runc/crun-class runtimes, namespaces/cgroups helpers or equivalent infrastructure.

### macOS

Linux containers require a Linux VM. Reuse Lima or another mature VM layer for VM lifecycle, filesystem sharing and networking. NAVISOMA should integrate with the runtime inside the VM rather than implementing a hypervisor.

### Windows

Use WSLC programmatic APIs where available.

Platform provisioning stays separate from Compose semantics.

## 11. Capability model

Capabilities are explicit and queryable. Categories include networking, storage, execution/TTY/I/O, resource controls, GPU, image operations and build support.

The planner should reject unsupported requirements before partial execution whenever possible.

## 12. State and reconciliation

`compose up` is desired-state reconciliation, not merely a list of create calls.

Useful identity inputs include project/service identity, normalized configuration digest, image digest, build result identity, network/volume identity and backend identity.

The exact persistence model remains a research item.

## 13. Error model

Public errors retain semantic layer identity:

```text
SourceError
SchemaError
ComposeSemanticError
PlanningError
CapabilityError
BuildError
RuntimeError
PlatformError
NativeInteropError
```

Native errors are preserved as diagnostics but translated at the integration boundary.

## 14. Observability

Measure parse/normalization, planning, image/build/runtime action latency, native/backend call counts, CPU/memory/I/O where relevant, cache/reconciliation decisions and FFI overhead when material.

Metrics must detect implementations that pass tests by doing unnecessary work.

## 15. Testing

### Compose differential tests

Compare canonical results against compose-go/reference behavior where meaningful.

### Native interop tests

Test ABI, ownership, cleanup, callbacks, errors, version mismatch and actual I/O for every C/C++ bridge.

### Protocol interoperability

Use upstream/native protobuf/gRPC implementations against real containerd/BuildKit endpoints; do not rely only on self-interoperability.

### Backend integration

Run equivalent scenarios against containerd and WSLC and compare semantic outcomes.

### Real-world corpus

Maintain representative Compose configurations exercising interpolation, merge, include, paths, dependencies, health conditions, volumes, networking and build.

## Dependency direction

```text
Existing native/Nim foundations
  YAML / JSON Schema / protobuf / gRPC / TLS
                    |
          thin bindings/shims/codegen
                    |
Compose semantics --+-- OCI vocabulary/facades
                    |
       containerd / BuildKit / WSLC facades
                    |
   canonical model + execution planner
                    |
                  CLI
```

Lower-level native libraries do not depend on NAVISOMA. Thin bindings should not absorb product semantics.
