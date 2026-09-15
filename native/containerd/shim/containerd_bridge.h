// Narrow extern "C" facade over the generated containerd gRPC/protobuf C++ stubs
// (native/containerd/generated/), built per docs/native-c-cpp-integration-plan.md's "Common
// ABI rules": opaque handles, no std::string/exceptions crossing the boundary, owned result
// handles for both values and errors.
//
// This is the *only* thing src/native/containerd_raw.nim (the raw Nim importc layer) talks to.
// Every operation here is exactly one of the 6 BackendPort operations (src/navisoma/backend.nim)
// — no backend-native handle or containerd concept (task, snapshot, namespace) is exposed beyond
// this file; the Nim side only ever sees NAVISOMA-level identifiers (image ref, service name).
#ifndef NAVISOMA_CONTAINERD_BRIDGE_H
#define NAVISOMA_CONTAINERD_BRIDGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// A connected containerd client (one gRPC channel + fixed namespace). One matching
// nvsm_containerd_close per nvsm_containerd_connect call.
typedef struct nvsm_containerd_client nvsm_containerd_client;

// Every call below returns one of these. Never null. Always release with
// nvsm_containerd_result_release exactly once. On failure (`nvsm_containerd_result_ok` == 0),
// the only other valid accessor is nvsm_containerd_result_error_message — no exception ever
// crosses this boundary; every failure path (containerd RPC error, gRPC transport error, local
// validation) is caught inside the shim and reported this way.
typedef struct nvsm_containerd_result nvsm_containerd_result;

int nvsm_containerd_result_ok(const nvsm_containerd_result* r);

// Valid when !ok. Pointer is owned by `r` and stays valid until release.
const char* nvsm_containerd_result_error_message(const nvsm_containerd_result* r, size_t* out_len);

// Valid when ok, for calls that return a string value (currently: resolve_image's resolved
// image reference). Pointer is owned by `r` and stays valid until release.
const char* nvsm_containerd_result_string_value(const nvsm_containerd_result* r, size_t* out_len);

// Valid when ok, for exec_health_probe: the probed process's exit code.
int nvsm_containerd_result_exit_code(const nvsm_containerd_result* r);

void nvsm_containerd_result_release(nvsm_containerd_result* r);

// Connects to containerd's gRPC socket (e.g. "/run/containerd/containerd.sock") and binds every
// subsequent call on the returned client to `containerd_namespace`. Returns NULL and sets
// *out_error (caller-owned, free with nvsm_containerd_free_string) on failure — there is no
// result handle to attach a connect-time error to, since there is no client to attach it to.
nvsm_containerd_client* nvsm_containerd_connect(
    const char* socket_path,
    const char* containerd_namespace,
    char** out_error);

void nvsm_containerd_free_string(char* s);

void nvsm_containerd_close(nvsm_containerd_client* client);

// --- The 6 BackendPort operations -----------------------------------------------------------

// Pulls `image_ref` into containerd's image store via the Transfer service (server-side pull —
// NAVISOMA never implements registry auth/content ingestion itself) and returns the resolved
// image reference as the result's string value.
nvsm_containerd_result* nvsm_containerd_resolve_image(
    nvsm_containerd_client* client,
    const char* image_ref, size_t image_ref_len);

// Prepares a rootfs snapshot (the "native" snapshotter — see native/containerd/dev-daemon.sh for
// why), creates the containerd Container record, and creates (but does not start) its task.
// `service_name` becomes the containerd container/task id directly — the MVP's fixed Compose
// service-name alphabet is containerd-id-safe, so no separate id-mapping table is needed.
nvsm_containerd_result* nvsm_containerd_create_container(
    nvsm_containerd_client* client,
    const char* service_name, size_t service_name_len,
    const char* resolved_image_id, size_t resolved_image_id_len,
    const char* const* command, const size_t* command_lens, size_t command_len,
    const char* const* env_keys, const size_t* env_key_lens,
    const char* const* env_values, const size_t* env_value_lens, size_t env_len);

nvsm_containerd_result* nvsm_containerd_start_container(
    nvsm_containerd_client* client,
    const char* service_name, size_t service_name_len);

// Runs `test` as a one-shot exec inside the already-started task and waits for it to exit.
// Result's exit code is the probe outcome (backend.nim's ProbeResult.exitCode) — the shim never
// interprets 0/nonzero itself, that's health.nim's job.
nvsm_containerd_result* nvsm_containerd_exec_health_probe(
    nvsm_containerd_client* client,
    const char* service_name, size_t service_name_len,
    const char* const* test, const size_t* test_lens, size_t test_len);

nvsm_containerd_result* nvsm_containerd_stop_container(
    nvsm_containerd_client* client,
    const char* service_name, size_t service_name_len);

// Deletes the task (if any), the container record, and the rootfs snapshot. Safe to call after
// stop_container or directly on a container whose task never started.
nvsm_containerd_result* nvsm_containerd_remove_container(
    nvsm_containerd_client* client,
    const char* service_name, size_t service_name_len);

#ifdef __cplusplus
}
#endif

#endif
