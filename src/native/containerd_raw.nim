## Raw importc declarations over native/containerd/shim/containerd_bridge.h — the C ABI
## boundary (docs/native-c-cpp-integration-plan.md "Common ABI rules": opaque handles, no
## std::string/exceptions crossing the boundary, owned result handles). Only
## src/bindings/containerd_client.nim may import this module; nothing above that wraps or
## interprets these raw types directly.
##
## Compiled and linked only under -d:navisomaContainerd, and only inside the pinned build
## container (native/containerd/Dockerfile) — the {.passC.}/{.passL.} flags below need the
## grpc/protobuf C++ dev headers and libs that image provides. Native toolchains for this
## project are never installed on the host; see native/containerd/generate.sh and dev-daemon.sh
## for the same container-based approach at the proto/daemon layer.

when not defined(navisomaContainerd):
  {.error: "src/native/containerd_raw.nim must only be imported under -d:navisomaContainerd".}

import std/os

const
  nativeDir = currentSourcePath().parentDir() / ".." / ".." / "native" / "containerd"
  generatedDir = nativeDir / "generated"
  shimDir = nativeDir / "shim"

{.passC: gorge("pkg-config --cflags protobuf grpc++").}
{.passL: gorge("pkg-config --libs protobuf grpc++").}
{.passC: "-I" & generatedDir.}
{.passC: "-I" & shimDir.}
{.passC: "-std=c++17".}

## Every generated .cc file under native/containerd/generated/, explicitly enumerated — Nim's
## {.compile.} pragma does not glob-expand `*` (confirmed: "cannot find: .../*.cc"), and the
## set only changes when native/containerd/generate.sh's file list changes (see that script and
## generated/MANIFEST.md), so this list is not expected to churn.
{.compile: "../../native/containerd/shim/containerd_bridge.cc".}
{.compile: "../../native/containerd/generated/services/containers/v1/containers.pb.cc".}
{.compile: "../../native/containerd/generated/services/containers/v1/containers.grpc.pb.cc".}
{.compile: "../../native/containerd/generated/services/images/v1/images.pb.cc".}
{.compile: "../../native/containerd/generated/services/images/v1/images.grpc.pb.cc".}
{.compile: "../../native/containerd/generated/services/snapshots/v1/snapshots.pb.cc".}
{.compile: "../../native/containerd/generated/services/snapshots/v1/snapshots.grpc.pb.cc".}
{.compile: "../../native/containerd/generated/services/tasks/v1/tasks.pb.cc".}
{.compile: "../../native/containerd/generated/services/tasks/v1/tasks.grpc.pb.cc".}
{.compile: "../../native/containerd/generated/services/transfer/v1/transfer.pb.cc".}
{.compile: "../../native/containerd/generated/services/transfer/v1/transfer.grpc.pb.cc".}
{.compile: "../../native/containerd/generated/services/content/v1/content.pb.cc".}
{.compile: "../../native/containerd/generated/services/content/v1/content.grpc.pb.cc".}
{.compile: "../../native/containerd/generated/types/descriptor.pb.cc".}
{.compile: "../../native/containerd/generated/types/descriptor.grpc.pb.cc".}
{.compile: "../../native/containerd/generated/types/metrics.pb.cc".}
{.compile: "../../native/containerd/generated/types/metrics.grpc.pb.cc".}
{.compile: "../../native/containerd/generated/types/mount.pb.cc".}
{.compile: "../../native/containerd/generated/types/mount.grpc.pb.cc".}
{.compile: "../../native/containerd/generated/types/platform.pb.cc".}
{.compile: "../../native/containerd/generated/types/platform.grpc.pb.cc".}
{.compile: "../../native/containerd/generated/types/task/task.pb.cc".}
{.compile: "../../native/containerd/generated/types/task/task.grpc.pb.cc".}
{.compile: "../../native/containerd/generated/types/transfer/imagestore.pb.cc".}
{.compile: "../../native/containerd/generated/types/transfer/imagestore.grpc.pb.cc".}
{.compile: "../../native/containerd/generated/types/transfer/registry.pb.cc".}
{.compile: "../../native/containerd/generated/types/transfer/registry.grpc.pb.cc".}

type
  NvsmContainerdClient {.importc: "nvsm_containerd_client", header: "containerd_bridge.h", incompleteStruct.} = object
  NvsmContainerdClientPtr* = ptr NvsmContainerdClient

  NvsmContainerdResult {.importc: "nvsm_containerd_result", header: "containerd_bridge.h", incompleteStruct.} = object
  NvsmContainerdResultPtr* = ptr NvsmContainerdResult

proc nvsmContainerdResultOk*(r: NvsmContainerdResultPtr): cint
  {.importc: "nvsm_containerd_result_ok", header: "containerd_bridge.h".}
proc nvsmContainerdResultErrorMessage*(r: NvsmContainerdResultPtr, outLen: ptr csize_t): cstring
  {.importc: "nvsm_containerd_result_error_message", header: "containerd_bridge.h".}
proc nvsmContainerdResultStringValue*(r: NvsmContainerdResultPtr, outLen: ptr csize_t): cstring
  {.importc: "nvsm_containerd_result_string_value", header: "containerd_bridge.h".}
proc nvsmContainerdResultExitCode*(r: NvsmContainerdResultPtr): cint
  {.importc: "nvsm_containerd_result_exit_code", header: "containerd_bridge.h".}
proc nvsmContainerdResultRelease*(r: NvsmContainerdResultPtr)
  {.importc: "nvsm_containerd_result_release", header: "containerd_bridge.h".}
proc nvsmContainerdFreeString*(s: cstring)
  {.importc: "nvsm_containerd_free_string", header: "containerd_bridge.h".}

proc nvsmContainerdConnect*(socketPath: cstring, containerdNamespace: cstring, outError: ptr cstring): NvsmContainerdClientPtr
  {.importc: "nvsm_containerd_connect", header: "containerd_bridge.h".}
proc nvsmContainerdClose*(client: NvsmContainerdClientPtr)
  {.importc: "nvsm_containerd_close", header: "containerd_bridge.h".}

proc nvsmContainerdResolveImage*(client: NvsmContainerdClientPtr,
    imageRef: cstring, imageRefLen: csize_t): NvsmContainerdResultPtr
  {.importc: "nvsm_containerd_resolve_image", header: "containerd_bridge.h".}

proc nvsmContainerdCreateContainer*(
    client: NvsmContainerdClientPtr,
    serviceName: cstring, serviceNameLen: csize_t,
    resolvedImageId: cstring, resolvedImageIdLen: csize_t,
    command: ptr UncheckedArray[cstring], commandLens: ptr UncheckedArray[csize_t], commandLen: csize_t,
    envKeys: ptr UncheckedArray[cstring], envKeyLens: ptr UncheckedArray[csize_t],
    envValues: ptr UncheckedArray[cstring], envValueLens: ptr UncheckedArray[csize_t], envLen: csize_t
  ): NvsmContainerdResultPtr
  {.importc: "nvsm_containerd_create_container", header: "containerd_bridge.h".}

proc nvsmContainerdStartContainer*(client: NvsmContainerdClientPtr,
    serviceName: cstring, serviceNameLen: csize_t): NvsmContainerdResultPtr
  {.importc: "nvsm_containerd_start_container", header: "containerd_bridge.h".}

proc nvsmContainerdExecHealthProbe*(
    client: NvsmContainerdClientPtr,
    serviceName: cstring, serviceNameLen: csize_t,
    test: ptr UncheckedArray[cstring], testLens: ptr UncheckedArray[csize_t], testLen: csize_t
  ): NvsmContainerdResultPtr
  {.importc: "nvsm_containerd_exec_health_probe", header: "containerd_bridge.h".}

proc nvsmContainerdStopContainer*(client: NvsmContainerdClientPtr,
    serviceName: cstring, serviceNameLen: csize_t): NvsmContainerdResultPtr
  {.importc: "nvsm_containerd_stop_container", header: "containerd_bridge.h".}

proc nvsmContainerdRemoveContainer*(client: NvsmContainerdClientPtr,
    serviceName: cstring, serviceNameLen: csize_t): NvsmContainerdResultPtr
  {.importc: "nvsm_containerd_remove_container", header: "containerd_bridge.h".}
