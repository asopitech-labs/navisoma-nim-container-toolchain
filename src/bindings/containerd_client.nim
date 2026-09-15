## Safe Nim wrapper over src/native/containerd_raw.nim's C ABI: owns every
## NvsmContainerdResultPtr (released before the wrapping proc returns) and translates every
## failure into NAVISOMA's own error taxonomy (docs/architecture.md section 13) — a plain
## RuntimeError carrying a NAVISOMA-level message, never the raw native handle or exception
## (per backend.nim's own boundary and #18's semantic failure contract: "Backend-native errors
## and handles never appear in planner contracts or CLI output").
##
## Only src/navisoma/backends/containerd_backend.nim may import this module.

when not defined(navisomaContainerd):
  {.error: "src/bindings/containerd_client.nim must only be imported under -d:navisomaContainerd".}

import ../native/containerd_raw
import ../navisoma/errors

type
  ContainerdClient* = object
    raw: NvsmContainerdClientPtr

proc toCStringArray(strs: seq[string]): (seq[cstring], seq[csize_t]) =
  ## Keeps the backing Nim strings alive via the returned seq[cstring]'s own lifetime — callers
  ## must keep both returned seqs alive for the duration of the native call they feed.
  result[0] = newSeq[cstring](strs.len)
  result[1] = newSeq[csize_t](strs.len)
  for i, s in strs:
    result[0][i] = s.cstring
    result[1][i] = s.len.csize_t

template asArray(cstrs: seq[cstring]): ptr UncheckedArray[cstring] =
  if cstrs.len > 0: cast[ptr UncheckedArray[cstring]](unsafeAddr cstrs[0]) else: nil

template asArray(lens: seq[csize_t]): ptr UncheckedArray[csize_t] =
  if lens.len > 0: cast[ptr UncheckedArray[csize_t]](unsafeAddr lens[0]) else: nil

proc connect*(socketPath, containerdNamespace: string): ContainerdClient =
  var errPtr: cstring
  let raw = nvsmContainerdConnect(socketPath.cstring, containerdNamespace.cstring, addr errPtr)
  if raw.isNil:
    let msg = if errPtr.isNil: "unknown connect error" else: $errPtr
    if not errPtr.isNil: nvsmContainerdFreeString(errPtr)
    raise newException(RuntimeError, "containerd connect(" & socketPath & "): " & msg)
  ContainerdClient(raw: raw)

proc close*(c: ContainerdClient) =
  nvsmContainerdClose(c.raw)

proc checkResult(r: NvsmContainerdResultPtr, context: string) =
  if nvsmContainerdResultOk(r) == 0:
    var len: csize_t
    let msg = nvsmContainerdResultErrorMessage(r, addr len)
    let text = if msg.isNil: "" else: $msg
    nvsmContainerdResultRelease(r)
    raise newException(RuntimeError, "containerd " & context & ": " & text)

proc resolveImage*(c: ContainerdClient, imageRef: string): string =
  let r = nvsmContainerdResolveImage(c.raw, imageRef.cstring, imageRef.len.csize_t)
  checkResult(r, "resolve_image(" & imageRef & ")")
  var len: csize_t
  let val = nvsmContainerdResultStringValue(r, addr len)
  result = if val.isNil: "" else: $val
  nvsmContainerdResultRelease(r)

proc createContainer*(c: ContainerdClient, serviceName, resolvedImageId: string,
                       command: seq[string], env: seq[tuple[key, value: string]]) =
  let (cmdC, cmdLens) = toCStringArray(command)
  var envKeys = newSeq[string](env.len)
  var envValues = newSeq[string](env.len)
  for i, kv in env:
    envKeys[i] = kv.key
    envValues[i] = kv.value
  let (envKeysC, envKeyLens) = toCStringArray(envKeys)
  let (envValuesC, envValueLens) = toCStringArray(envValues)

  let r = nvsmContainerdCreateContainer(
    c.raw,
    serviceName.cstring, serviceName.len.csize_t,
    resolvedImageId.cstring, resolvedImageId.len.csize_t,
    asArray(cmdC), asArray(cmdLens), command.len.csize_t,
    asArray(envKeysC), asArray(envKeyLens),
    asArray(envValuesC), asArray(envValueLens), env.len.csize_t)
  checkResult(r, "create_container(" & serviceName & ")")

proc startContainer*(c: ContainerdClient, serviceName: string) =
  checkResult(nvsmContainerdStartContainer(c.raw, serviceName.cstring, serviceName.len.csize_t),
              "start_container(" & serviceName & ")")

proc execHealthProbe*(c: ContainerdClient, serviceName: string, test: seq[string]): int =
  let (testC, testLens) = toCStringArray(test)
  let r = nvsmContainerdExecHealthProbe(
    c.raw, serviceName.cstring, serviceName.len.csize_t,
    asArray(testC), asArray(testLens), test.len.csize_t)
  checkResult(r, "exec_health_probe(" & serviceName & ")")
  result = nvsmContainerdResultExitCode(r).int
  nvsmContainerdResultRelease(r)

proc stopContainer*(c: ContainerdClient, serviceName: string) =
  checkResult(nvsmContainerdStopContainer(c.raw, serviceName.cstring, serviceName.len.csize_t),
              "stop_container(" & serviceName & ")")

proc removeContainer*(c: ContainerdClient, serviceName: string) =
  checkResult(nvsmContainerdRemoveContainer(c.raw, serviceName.cstring, serviceName.len.csize_t),
              "remove_container(" & serviceName & ")")
