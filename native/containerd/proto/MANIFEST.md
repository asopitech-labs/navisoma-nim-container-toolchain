# Vendored containerd proto source

## Provenance

- Upstream: https://github.com/containerd/containerd
- Pinned tag: `v2.3.5`
- Pinned commit: `1294c24a7da8e5a793ed378161673abe94118892`
- Vendored subtree: `api/` (all `*.proto` files, upstream `api/` directory prefix stripped —
  e.g. upstream `api/types/mount.proto` is here at `types/mount.proto`)
- License: Apache-2.0 (containerd's own; see upstream `LICENSE`/`NOTICE.md`)
- Vendored: 2026-09-15

These are proto **source** only — no generated `.pb.go`/`.go` files were copied. Nothing here
depends on Go; it is consumed by `protoc`/`grpc_cpp_plugin` inside `native/containerd/Dockerfile`
to produce the C++ stubs committed under `native/containerd/generated/` (locked decision:
generated bindings are committed to source control, `protoc` is a regeneration-time tool only).

## Why the whole `api/` tree, not just the 5 services this project uses

`resolveImage`/`createContainer`/`startContainer`/`stopContainer`/`removeContainer`/
`execHealthProbe` only need `services/{transfer,containers,tasks,images,snapshots}/v1` plus
their transitive `types/*` imports. But the whole vendored tree is 224K, so the full `api/`
subtree is kept for straightforward re-vendoring and future reference (e.g. Phase 4/5 needs)
without a second partial-vendor pass. **Only the 5 services above (and whatever they
transitively import) are actually passed to `protoc` for code generation** — see
`native/containerd/generated/MANIFEST.md`. Unused proto files here generate no C++ code and
add no compiled surface.

## Regenerating (bumping the pinned containerd version)

1. Re-clone containerd at the new tag, e.g.:
   `git clone --depth 1 --branch <new-tag> https://github.com/containerd/containerd.git`
2. Replace every file under this directory with the new `api/*.proto` tree (prefix-stripped
   the same way).
3. Update the provenance fields above (tag, commit, vendored date).
4. Re-run codegen (`native/containerd/Dockerfile` codegen stage) and commit the regenerated
   `native/containerd/generated/` output in the **same commit** as the proto bump (locked
   decision: a version bump and its regenerated bindings must land atomically).
5. Re-check `native/containerd/shim/containerd_bridge.cc` against the new stubs — a
   non-patch containerd bump can add/rename fields even within its non-major compatibility
   guarantee.
