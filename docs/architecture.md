# NAVISOMA Architecture

## Architectural thesis

NAVISOMA should be designed as a specification-driven execution system rather than a Docker-compatible CLI wrapped around multiple foreign tools.

A useful compiler analogy is:

```text
Compose source
    -> frontend processing
    -> canonical application model
    -> execution graph
    -> runtime/build lowering
    -> platform implementation
```

This structure keeps Compose semantics, scheduling semantics, runtime semantics, and platform mechanics independently testable.

## Layers

### 1. Source and specification frontend

Inputs may include:

- Compose YAML files
- environment variables and `.env` inputs
- included/extended Compose documents
- build contexts and build definitions
- referenced OCI images/artifacts

The Compose frontend is responsible for specification-defined processing, not container execution.

Suggested processing model:

```text
Raw YAML
   |
YAML AST/document
   |
per-file interpolation
   |
merge/include/extends processing
   |
schema validation
   |
defaults and normalization
   |
path resolution
   |
semantic consistency checks
   |
Canonical Compose Project
```

Exact ordering must follow the Compose Specification/reference behavior and should be captured in tests rather than inferred from this diagram.

### 2. Canonical application model

The canonical model represents what the application declares without embedding containerd or WSLC handles/types.

Representative concepts include:

- project
- service
- image/build source
- command/entrypoint
- environment
- mount
- network attachment
- published port
- health check
- dependency condition
- resource request/limit
- secret/config
- volume/network declarations

The model should be serializable to a stable diagnostic representation so different frontends/reference implementations can be compared in tests.

### 3. Execution graph

The planner lowers the application model into explicit actions and dependencies.

Representative action kinds:

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

A Compose dependency such as a service waiting for another service to become healthy becomes graph dependencies rather than imperative special cases scattered through runtime code.

The execution graph enables:

- parallel image pulls/builds where dependencies permit;
- critical dependency visibility;
- deterministic ordering;
- dry-run and explain output;
- selective reconciliation;
- failure propagation policies;
- deterministic teardown;
- metrics at action boundaries.

### 4. Runtime-neutral container semantics

NAVISOMA defines its own semantic interface. It must not simply mirror Docker Engine, containerd, or WSLC API shapes.

Representative operations include:

```text
Pull/resolve image
Create container
Start container
Stop container
Remove container
Execute process
Inspect state
Stream logs/I/O
Create/remove volume
Create/remove network
Query runtime capabilities
```

The interface uses OCI concepts where suitable and exposes capability differences explicitly.

### 5. Runtime backends

#### containerd backend

Target environments:

- Linux
- WSL
- macOS Linux VM

The backend communicates with containerd through its protobuf/gRPC APIs. A native Nim client is preferred so the NAVISOMA process does not depend on a Go bridge or repeatedly shell out to nerdctl.

#### WSL Containers backend

Target environment:

- Windows with WSL Containers

The backend lowers the same runtime-neutral semantics into the WSL Container API. Native C ABI bindings are preferred where the supported API surface and stability are sufficient.

WSLC is not a second product and does not receive a separate Compose implementation.

### 6. Build backends

Building is separate from running.

```text
Build action
    |
Build backend interface
    |
    +-- BuildKit / LLB
    +-- future builders
```

This allows, for example, BuildKit to produce an OCI image that is subsequently consumed by either containerd or WSLC without coupling the execution backend to the builder.

### 7. Platform provisioning

#### Linux

Use native runtime services where available.

#### WSL

Use the Linux runtime path where appropriate.

#### macOS

Ensure/manage a Linux VM, initially using an existing VM solution rather than building a hypervisor. Runtime communication should occur through the selected backend API once the VM is available.

#### Windows

Use the WSL Containers backend where supported.

Provisioning and execution remain separate layers so VM lifecycle does not leak into Compose semantics.

## Capability model

Different runtimes evolve at different speeds. NAVISOMA should represent support explicitly rather than pretending every backend has identical capabilities.

Example capability categories:

```text
Networking
  - published TCP/UDP ports
  - host/container networking modes
  - aliases/DNS

Storage
  - bind mounts
  - named volumes
  - read-only mounts

Execution
  - exec
  - TTY
  - stdin attachment
  - signal/stop semantics

Resources
  - CPU
  - memory
  - GPU

Images/build
  - pull
  - push
  - local import/export
  - build support
```

The planner can then produce a clear unsupported-feature diagnostic before partially executing a project.

## State and reconciliation

`compose up` should not be modeled only as a sequence of create calls. The engine should be able to compare desired state with observed state.

Potential state identity includes:

- project identity
- service identity
- normalized configuration digest
- image digest
- build result identity
- network/volume identity
- runtime backend identity

This enables selective recreation when configuration changes instead of unconditional teardown/recreate.

The exact persistence model remains a research item.

## Error model

Errors should retain layer identity:

```text
SourceError
SchemaError
ComposeSemanticError
PlanningError
CapabilityError
BuildError
RuntimeError
PlatformError
```

Backend-native error details should be preserved as diagnostics but should not become the public semantic error taxonomy.

## Observability

Execution graph actions provide natural telemetry boundaries. Useful measurements include:

- parse/normalization time
- planning time
- image resolution/pull time
- build time
- container creation/start latency
- health wait time
- CPU/memory/I/O where available
- backend API call counts/latency
- cache/reconciliation decisions

This supports both performance work and detection of implementations that technically pass tests while doing unnecessary or incorrect work.

## Testing strategy

### Unit/specification tests

Each reusable package tests its own specification semantics.

### Differential tests

For Compose, normalize known fixtures through both the Nim implementation and relevant reference implementations such as compose-go and compare canonical representations where semantics permit.

### Protocol interoperability

Protobuf/gRPC components should interoperate with reference implementations in other languages, not merely with themselves.

### Backend integration tests

Run equivalent execution scenarios against containerd and WSLC and compare semantic outcomes rather than backend-internal identifiers.

### Real-world corpus

Maintain a corpus of real Compose configurations covering common and difficult features. This is necessary because schema-level tests alone do not exercise interaction effects among interpolation, merge, includes, paths, dependencies and runtime capabilities.

## Dependency direction

The intended dependency direction is one-way:

```text
YAML / JSON Schema / protobuf foundations
        |
Compose / OCI / gRPC specification libraries
        |
containerd / BuildKit / WSLC clients
        |
NAVISOMA canonical model and planner
        |
runtime/build/platform implementations
        |
CLI
```

Lower-level reusable libraries must not depend on the NAVISOMA CLI or orchestration engine.
