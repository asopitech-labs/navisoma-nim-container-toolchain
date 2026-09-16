# containerd lowering: implementation record (#18 phase 3)

This records what the containerd `BackendPort` adapter actually turned out to need, beyond
`docs/native-c-cpp-integration-plan.md`'s general plan — so the same container-based workflow
and the gotchas below don't need rediscovering for BuildKit (or a future containerd version
bump).

## Pinned version

containerd `v2.3.5`, commit `1294c24a7da8e5a793ed378161673abe94118892`. See
`native/containerd/proto/MANIFEST.md` and `native/containerd/generated/MANIFEST.md` for full
provenance and the regeneration procedure.

## Layout

```
native/containerd/
  proto/                 vendored upstream .proto source (native/containerd/proto/MANIFEST.md)
  generated/             committed protoc/grpc_cpp_plugin C++ output (generated/MANIFEST.md)
  shim/containerd_bridge.{h,cc}   the extern "C" facade; shim/shim_smoke_test.cc exercises it
                                   directly against a live daemon (no Nim involved)
  Dockerfile             the build container: pinned protoc/grpc/protobuf + nlohmann-json + git
                          + Nim + the `yaml` nimble package — every native-toolchain build for
                          this project happens inside it, never on the host
  Dockerfile.daemon, config.toml, entrypoint.sh   the disposable containerd daemon image
  generate.sh            regenerate generated/ from proto/ (only on a version bump)
  dev-daemon.sh          up/down the disposable daemon (never a persistent fixture)
  test-shim.sh           native-layer smoke test (shim only, no Nim)

src/native/containerd_raw.nim        importc + {.compile.}/{.passC.}/{.passL.} — only compiles
                                      under -d:navisomaContainerd, inside the build container
src/bindings/containerd_client.nim   safe wrapper, owns every result handle, translates every
                                      failure to RuntimeError
src/navisoma/backends/containerd_backend.nim   the BackendPort adapter itself

tests/integration/containerd/run.sh   the full CLI, real-daemon scenario test (issue #18's
                                       "disposable real-host scenario") — healthy up/down and
                                       the unhealthy-dependency cleanup path, both against a
                                       fresh daemon
```

Building/testing anything under `native/containerd/` never installs a native toolchain or runs
a daemon on the host — always inside a container (podman, confirmed working; `docker` also
supported via `CONTAINER_ENGINE=docker`).

## Gotchas discovered (all fixed in the files above; recorded here so they don't recur)

- **Nim's `{.compile.}` pragma does not glob-expand `*`.** `src/native/containerd_raw.nim`
  enumerates all 24 generated `.cc` files explicitly. If `generate.sh`'s file list changes, this
  list needs updating too.
- **Nested overlayfs fails under rootless podman.** The daemon (and the shim's own
  `Snapshots.Prepare` calls) use the `native` snapshotter, not containerd's own default
  `overlayfs` — see `dev-daemon.sh`'s own comment.
- **containerd's cgroup v2 "no internal process" rule blocks a naive nested daemon.** Rootless
  podman gives the daemon container a private cgroup v2 namespace with containerd itself sitting
  directly in its root — `entrypoint.sh` relocates containerd into a child cgroup and delegates
  controllers before exec'ing it, or every task creation fails with "cannot enter cgroupv2 ...
  invalid state".
- **The transfer plugin's default unpack config only knows the daemon-default snapshotter.**
  Requesting `Transfer` with `unpacks: [{snapshotter: "native"}]` fails with "no unpack platforms
  defined" unless the daemon's `config.toml` explicitly registers that
  platform+snapshotter+differ combination under
  `[[plugins."io.containerd.transfer.v1.local".unpack_config]]` — see `config.toml`.
- **`google.protobuf.Any.type_url` for a containerd-defined proto message** (e.g. `OCIRegistry`,
  `ImageStore`) **is the bare full proto name** (`containerd.types.transfer.OCIRegistry`), not
  the `type.googleapis.com/...` convention `Any::PackFrom` would produce — confirmed empirically
  against the live daemon, not assumed.
- **`google.protobuf.Any.type_url` for the OCI runtime-spec types is
  `types.containerd.io/opencontainers/runtime-spec/<major>/<TypeName>`, JSON-encoded** (not
  protobuf) — confirmed from containerd's `core/runtime/typeurl.go` registration, since this one
  is genuinely not discoverable by probing alone.
- **The rootfs "parent" snapshot key is the image's layer-chain ID**, computed client-side (every
  containerd client does this, including containerd's own Go client) from the image's manifest →
  config → `rootfs.diff_ids`, using the standard `opencontainers/image-spec/identity` algorithm:
  `chain[0] = diffIds[0]`, `chain[i] = sha256(chain[i-1] + " " + diffIds[i])`. Implemented as a
  small self-contained SHA-256 in `containerd_bridge.cc` rather than reaching into gRPC's
  transitively-linked OpenSSL/BoringSSL, which isn't a stable API this project should depend on.
- **An empty stdio path on `CreateTaskRequest`/`ExecProcessRequest` is not "no redirection."** It
  leaves stdout/stderr disconnected, and a process that writes to a disconnected fd can itself
  exit non-zero (observed: busybox `echo` exited 1 with empty stdio, `false` — which writes
  nothing — exited correctly). Both requests redirect to `/dev/null` explicitly.
- **`execHealthProbe` reuses the container's own env and numeric uid/gid**, cached by the shim
  at `create_container` time (`nvsm_container_runtime_info`) and looked up by container id at
  probe time — `BackendPort`'s interface (`src/navisoma/backend.nim`) never carries environment
  itself, so this state has to live on the native side of the boundary. Falls back to a bare
  `PATH` only if that lookup somehow misses.
- **The image's `User` config is honored, numeric forms only** (`"1000"` or `"1000:1000"`) — a
  named user/group would require reading `/etc/passwd`/`/etc/group` out of the image's rootfs,
  which this shim does not do; such an image is rejected explicitly (`create_container` fails)
  rather than silently running as root.

## Verifying this still works

```bash
native/containerd/test-shim.sh              # shim only, ~2 min
tests/integration/containerd/run.sh          # full CLI against a real daemon, ~2 min
```

Both build fresh, run, and tear down completely — safe to run repeatedly, never leave state
behind.
