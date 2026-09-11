# Nim Container Ecosystem Research

## Scope

This document records the current conclusion about what NAVISOMA should build versus reuse.

The key finding is that Nim's C/C++ interoperability materially changes the project boundary. NAVISOMA does **not** need a language-native reimplementation for every cloud-native subsystem. Mature C/C++ libraries can often be consumed directly through `importc`, `importcpp`, generated bindings, or a thin shim.

The important gap is therefore not "Nim lacks all infrastructure." The gap is a coherent Nim-native semantic/orchestration layer that can compose existing native infrastructure into one cross-platform container workflow.

## Decision rule

For every subsystem, evaluate in this order:

1. stable C ABI;
2. thin C/C++ shim;
3. narrow `importcpp` integration;
4. generated bindings/code;
5. existing maintained Nim library;
6. new Nim implementation only when the previous options are inadequate.

This ordering is now part of the architecture.

## Compose Specification

Compose remains one of the strongest areas for NAVISOMA-owned implementation because the important behavior is semantic rather than merely transport/library plumbing.

Required behavior includes interpolation, merge, include, extends, defaults, uniqueness, path resolution, normalization and consistency rules.

### Direction

A reusable Compose semantic library remains justified. Generic YAML and JSON Schema engines should be reused rather than embedded as NAVISOMA inventions.

`compose-go`, Docker Compose and nerdctl remain specification/reference/oracle sources.

## YAML

NimYAML is the default reuse candidate for YAML 1.2 parsing.

### Direction

Do not create a NAVISOMA YAML implementation.

## JSON Schema

The earlier plan considered a new Nim Draft 2020-12 implementation. That is no longer the default.

Mature C++ JSON Schema implementations can be consumed through Nim's C++ backend or a thin C-compatible shim. This removes most justification for a NAVISOMA-specific validator unless interoperability, portability or packaging evidence proves those options inadequate.

### Direction

Reuse a mature native validator. Own only the Compose-specific schema selection/error integration required by the frontend.

## Protobuf

Official protobuf C++ runtime and `protoc` generated code provide a mature implementation with descriptors, reflection and well-known-type support.

Existing Nim protobuf projects remain worth evaluating, but the absence of a perfect Nim package is not a reason to reimplement wire/runtime semantics.

### Direction

Prefer official generated/native code plus a narrow Nim boundary where that produces better compatibility and maintenance characteristics. New protobuf runtime work requires explicit evidence.

## gRPC

gRPC provides native C/C++ implementations. The C core exists specifically as a low-level basis for higher-level language bindings, and the C++ stack can also be used behind a shim.

Joubako remains relevant as a Nim-native alternative and source of experience, but NAVISOMA no longer assumes it must create `nvsm-grpc-nim`.

### Direction

Use gRPC Core C API or gRPC C++ plus generated code where feasible. Evaluate Unix-domain sockets, streaming, deadlines/cancellation, metadata, completion behavior and integration with Nim async/threading. Hide the selected transport behind containerd/BuildKit facades rather than exposing gRPC throughout NAVISOMA.

## containerd

containerd remains an external runtime. Its APIs are protobuf/gRPC based.

### Direction

Do not build a generic Nim gRPC stack for containerd. Generate the upstream protocol clients and reuse mature native protobuf/gRPC runtimes. NAVISOMA owns only the containerd-facing facade, operation sequencing/lifecycle translation and lowering from the execution graph.

The official containerd Go client and nerdctl are essential references for real API sequencing, namespaces, content/images, snapshots, tasks, leases, events, streaming, cleanup and error behavior.

A standalone `nvsm-containerd-client-nim` repository is conditional on independent reuse value.

## OCI specifications

OCI remains the preferred vocabulary for images, descriptors, manifests, indexes, configuration, digests, platforms and distribution.

Earlier plans assumed a language-native OCI package would probably be created. That is now conditional.

### Direction

First evaluate mature OCI libraries and reference implementations, including ORAS, containers/image, go-containerregistry and official OCI conformance/reference code. Reuse native functionality where practical and expose only the subset of OCI concepts the NAVISOMA canonical model requires.

Create `nvsm-oci-spec-nim` or `nvsm-oci-client-nim` only if substantial independently useful code remains after reuse.

## OCI Distribution / registry

Registry behavior includes authentication, manifests, blobs, uploads/downloads, redirects/retries, platform selection, referrers and digest verification.

### Direction

Do not assume NAVISOMA must write a registry implementation. Prefer a mature existing implementation reachable through a stable native boundary or a narrow facade. Use ORAS/go-containerregistry/containers-image as behavior and design references where direct linking is not practical.

## BuildKit and LLB

BuildKit already supplies the build solver, cache model and LLB execution machinery.

### Direction

NAVISOMA must not recreate the solver. Compose `build:` is lowered into NAVISOMA build actions, which then use generated BuildKit/LLB protocol code and mature protobuf/gRPC runtime infrastructure.

A `nvsm-buildkit-client-nim` repository is justified only if the resulting client facade is independently useful.

## WSL Containers

WSL Container API is a particularly strong Nim fit because a C projection exists.

### Direction

Prefer direct `importc` to the stable C surface. Introduce a thin C/C++ shim only where the raw API has ownership, type or ABI complexity that should not leak into Nim.

NAVISOMA owns safe lifecycle wrappers, capability normalization, error translation and lowering from the common execution model. It does not recreate WSLC behavior.

A standalone `nvsm-wslc-client-nim` remains a strong candidate if the binding is useful independently.

## Networking and low-level runtime

CNI, runc/crun-class runtimes, namespaces/cgroups tooling, rootless infrastructure and similar components already exist and are widely exercised.

### Direction

Reuse them. NAVISOMA should integrate existing runtime/network facilities through containerd/platform APIs rather than creating Nim versions.

## macOS

Linux containers require a Linux VM.

### Direction

Reuse Lima or another mature VM layer for VM lifecycle, filesystem sharing and networking. NAVISOMA should manage/integrate the VM but not implement a hypervisor.

## Async and threading

The important question is no longer which Nim async ecosystem should become the protocol implementation substrate for everything. Native libraries may own their own completion/thread models.

### Direction

Define explicit bridge rules for callbacks, completion queues, thread entry into Nim, cancellation, ownership and blocking versus async calls. Keep pure model/parser libraries independent of transport runtimes.

## Packaging becomes a first-class concern

Native reuse reduces source-code ownership but adds dependency-distribution responsibility.

NAVISOMA must define:

- native version pinning;
- source/prebuilt/system-package acquisition;
- static versus dynamic linking;
- reproducible builds and cache identities;
- ABI compatibility checks;
- license/NOTICE/provenance handling;
- security/CVE update policy;
- target-specific packaging for Linux, WSL, macOS and Windows.

This is not incidental build plumbing; it is part of the product architecture.

## Revised research conclusion

NAVISOMA should directly invest in:

1. Compose semantic processing and canonical representation.
2. Canonical application model.
3. Execution graph/planning/reconciliation.
4. Capability model and cross-platform backend selection.
5. Thin safe bindings/facades for containerd, BuildKit and WSLC where programmatic integration requires them.
6. Native dependency packaging/version/provenance infrastructure.
7. Differential, conformance, ABI and real backend testing.

NAVISOMA should **not** plan new implementations of YAML, JSON Schema, protobuf, gRPC, HTTP/2, TLS, CNI, low-level OCI runtimes, BuildKit solving or macOS virtualization unless a later evidence-backed investigation demonstrates a real gap.

The project is now better described as a Nim-native semantic and integration toolchain over mature native systems, not a parallel reimplementation of the cloud-native stack.
