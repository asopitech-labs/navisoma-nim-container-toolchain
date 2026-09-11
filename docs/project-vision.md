# NAVISOMA Project Vision

## Purpose

NAVISOMA explores and builds a cross-platform container toolchain in Nim while strengthening the reusable container and cloud-native infrastructure available to the Nim ecosystem.

The originating problem is straightforward: a Compose-defined application should be operable through the same user-facing model on Linux, WSL, macOS, and Windows with WSL Containers without requiring Docker to be the common runtime or architectural center.

The broader opportunity is that implementing this well requires capabilities that are currently fragmented or missing in Nim: Compose semantics, OCI models and registry access, containerd integration, WSL Container API integration, and potentially stronger protobuf/gRPC and JSON Schema infrastructure. NAVISOMA treats those missing pieces as reusable ecosystem work rather than private implementation details.

## Problem statement

Existing cross-platform container workflows commonly converge on Docker-compatible APIs or a particular runtime implementation. That is useful for compatibility, but it couples the portable application model to a vendor/runtime architecture.

NAVISOMA instead separates four concerns:

1. **Portable specifications** — Compose, OCI, protobuf/gRPC and related standards.
2. **Semantic application/execution model** — what must be built, created, connected, started, observed, stopped, or removed.
3. **Runtime/build implementations** — containerd, WSL Containers, BuildKit and future implementations.
4. **Platform provisioning** — native Linux, WSL, macOS Linux VM management, and Windows.

The user should not have to choose a different orchestration model because one host uses containerd and another uses WSL Containers.

## Goals

### Cross-platform operational consistency

Provide a coherent CLI and application lifecycle across:

- Linux
- WSL
- macOS, using a managed Linux VM where Linux containers require one
- Windows using WSL Containers

Platform implementation differences must remain below the common semantic interface unless a capability is genuinely unavailable.

### Native Compose ecosystem for Nim

Implement the Compose Specification as a reusable Nim library rather than embedding ad-hoc YAML handling in the final CLI. Compose processing includes substantially more than deserialization: interpolation, merge semantics, include/extends handling, defaults, path resolution, normalization, consistency validation, and canonical project modeling.

### OCI as a first-class vocabulary

Represent images, descriptors, manifests, indexes, digests, platforms, layouts, distribution operations, and related concepts using OCI terminology and specifications wherever practical. Docker-specific concepts should not leak into the core model when an OCI concept exists.

### Explicit planning and execution

Translate a canonical application model into an explicit execution graph. Operations may include:

- resolve/pull image
- build image
- create network
- create volume
- create container
- start container
- await health/readiness condition
- execute process
- stop container
- remove container/resource

The graph is the basis for scheduling, parallelism, reconciliation, observability, dry-run/explain output, and deterministic teardown.

### Independent build and runtime layers

Image construction and container execution are different responsibilities. BuildKit integration should not force a particular runtime, and a runtime's built-in build functionality should not define the architecture.

### Strengthen Nim infrastructure through real workloads

NAVISOMA should provide an end-to-end consumer for its lower-level libraries. A protocol implementation is not considered useful merely because a toy RPC succeeds; containerd and BuildKit provide realistic workloads for protobuf/gRPC. OCI registry operations provide realistic HTTP/content-addressing workloads. Compose provides a complex configuration-language workload for YAML, JSON Schema, interpolation, normalization, and semantic validation.

## Non-goals

NAVISOMA is not defined as:

- a Docker Desktop clone;
- a Docker daemon reimplementation;
- a wrapper that shells out to Docker on every platform;
- a Compose-to-WSLC translator with Windows treated as a separate product;
- an excuse to reimplement mature Nim libraries that already satisfy the required specification and maintenance criteria;
- a single monolithic repository containing every reusable protocol and specification implementation.

Docker and Docker Compose remain important compatibility/reference implementations, but they are not the internal architecture.

## Umbrella model

NAVISOMA is both the umbrella for the ecosystem effort and the name associated with the integrated container toolchain. Reusable libraries may live in separate repositories under the `nvsm-` namespace.

The naming pattern is deliberately descriptive:

```text
nvsm-<upstream-or-specification>-nim
```

or, when the component is specifically a client/binding:

```text
nvsm-<upstream>-client-nim
```

This preserves recognizable upstream terminology while preventing the repositories from appearing to be official upstream implementations.

## Success criteria

Success is not merely "a Compose file starts containers." Evidence should include:

- specification/conformance coverage;
- compatibility against relevant reference implementations;
- deterministic canonical models;
- correct dependency/lifecycle semantics;
- capability-aware behavior across supported backends;
- real containerd and WSL Container workloads;
- reusable, independently documented Nim packages;
- performance/resource measurements for parsing, planning and execution where they materially affect user experience;
- explainable execution decisions rather than opaque orchestration.

## Research posture

Implementation choices remain evidence-driven. Existing Nim projects are evaluated for specification coverage, API quality, maintenance, tests, portability and integration cost before a new library is created. NAVISOMA should fill ecosystem gaps, not manufacture duplicate packages for organizational symmetry.
