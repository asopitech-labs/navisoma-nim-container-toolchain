# NAVISOMA Project Vision

## Purpose

NAVISOMA builds a cross-platform container toolchain in Nim for Linux, WSL, macOS, and Windows with WSL Containers.

Nim is selected deliberately because it can sit directly on top of mature C and C++ infrastructure. NAVISOMA is therefore **not** a project to recreate the cloud-native ecosystem in Nim. It is a project to own the semantic and orchestration layers that are genuinely missing while reusing established native implementations for protocol, validation, runtime, networking, build, VM, and low-level container responsibilities.

The central design question is not "how do we rewrite this in Nim?" but:

> What is the smallest NAVISOMA-owned semantic layer that can compose mature native systems into one coherent cross-platform container workflow?

## Problem statement

A Compose-defined application should be operable through the same user-facing model on Linux, WSL, macOS, and Windows with WSL Containers without requiring Docker to be the architectural center.

Existing components already solve much of the lower stack:

- containerd provides container lifecycle/content/snapshot/task services;
- BuildKit provides image-build solving and LLB;
- WSL Containers provides a native Windows container API;
- OCI specifications define portable image/runtime/distribution vocabulary;
- CNI, runc/crun-class runtimes, Lima and similar projects already solve lower platform concerns;
- protobuf, gRPC, HTTP/2, TLS, JSON and JSON Schema have mature native implementations.

NAVISOMA should integrate those systems, not compete with them unnecessarily.

## Architectural ownership

NAVISOMA directly owns the layers where its product value exists:

1. **Compose semantics** — interpolation, merge/include/extends behavior, normalization, consistency rules and canonical project representation where these are not delegated safely to generic libraries.
2. **Canonical application model** — runtime-neutral representation of services, images/builds, networks, mounts, ports, resources, health/dependency conditions and desired state.
3. **Execution graph and planner** — explicit actions, dependencies, scheduling, reconciliation, explainability and deterministic teardown.
4. **Capability model** — representing real backend/platform differences without exposing different user workflows.
5. **Backend lowering/facades** — thin translation from NAVISOMA semantics to containerd, BuildKit, WSLC and selected platform services.
6. **Cross-platform CLI and lifecycle** — one operational model across supported hosts.

NAVISOMA does **not** automatically own the protocol/runtime implementations beneath those boundaries.

## Native reuse policy

For an external capability, evaluate integration in this order:

1. Stable C ABI via `importc`.
2. Thin C/C++ shim that exposes a stable narrow ABI over an existing implementation.
3. Narrow direct C++ integration via `importcpp`.
4. Generated bindings/code using upstream schemas or IDLs.
5. Existing maintained Nim library.
6. New Nim implementation only when the previous options are demonstrably inadequate.

This is a core architectural rule, not an optimization for later.

Examples of components expected to be reused rather than recreated include generic protobuf/gRPC transports, HTTP/2/TLS, JSON Schema validation, CNI networking, low-level OCI runtimes, BuildKit solving, and macOS virtualization infrastructure.

## Goals

### Cross-platform operational consistency

Provide one coherent CLI and lifecycle model across Linux, WSL, macOS and Windows/WSLC. Platform implementation differences stay below the canonical model unless a capability is genuinely unavailable.

### Compose semantics as first-class product logic

Compose is more than YAML deserialization. NAVISOMA must understand specification-defined processing and must be testable against Compose reference behavior. Generic YAML/JSON Schema work should be delegated to existing libraries where practical; Compose-specific behavior remains owned.

### Explicit planning and execution

Translate canonical desired state into an execution graph containing operations such as image resolution/pull, build, network/volume creation, container creation/start, health waiting, exec, stop and removal.

The graph is the basis for scheduling, parallelism, reconciliation, observability, dry-run/explain output and deterministic teardown.

### Independent build and runtime layers

BuildKit or another builder is selected independently of container execution. A runtime backend must not silently become the build architecture.

### OCI as vocabulary, not a rewrite mandate

Use OCI terminology and structures where they provide the portable concepts NAVISOMA needs. Do not infer that adopting OCI requires writing a complete language-native OCI ecosystem.

### Thin, reusable integration surfaces

Bindings/facades may become independent packages when they have clear reuse value. A repository is not created merely because an external system exists.

## Non-goals

NAVISOMA is not:

- a Docker Desktop clone;
- a Docker daemon reimplementation;
- a reimplementation of containerd, BuildKit, CNI, runc/crun or Lima;
- a new gRPC/protobuf/HTTP2/TLS/JSON Schema ecosystem by default;
- a Compose-to-WSLC translator with a separate Windows orchestration path;
- a repository factory for `nvsm-*` packages;
- a wrapper that shells out to Docker for every operation.

Shelling out to an existing CLI can be used as a bootstrap or fallback when justified, but stable programmatic native APIs are preferred for the product architecture.

## Repository model

The main repository is `navisoma-nim-container-toolchain`.

The `nvsm-` namespace is reserved for independently useful code that NAVISOMA actually owns or maintains. Current repository classes are:

- **strong candidate:** Compose semantic implementation;
- **strong/conditional binding candidate:** WSLC safe Nim client;
- **conditional facade candidates:** containerd and BuildKit clients;
- **conditional OCI packages:** only if substantial owned behavior remains after reuse;
- **not default candidates:** gRPC, protobuf, JSON Schema, HTTP/2, TLS, YAML implementations.

Repository extraction follows demonstrated ownership and reuse value, not symmetry.

## Success criteria

Success is not merely "a Compose file starts containers." Evidence should include:

- Compose specification/reference compatibility;
- deterministic canonical models;
- correct dependency/lifecycle semantics;
- capability-aware behavior across supported backends;
- real containerd, BuildKit and WSLC workloads;
- proven C/C++ FFI ownership/error/threading correctness;
- reproducible native dependency acquisition and linking;
- performance/resource measurements where material;
- explainable execution decisions;
- minimal duplicated implementation relative to mature upstream systems.

## Research posture

Before writing a subsystem, inspect its specification, mature production implementations, language bindings, C/C++ APIs, generated-code options, conformance tests, licenses and packaging model. New code must have a clear reason to exist beyond "there is no Nim package with this exact name."
