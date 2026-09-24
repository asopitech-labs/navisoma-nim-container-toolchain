## Safe WSLC SDK ownership layer. Only the WSLC backend imports this module;
## callers receive NAVISOMA identifiers and RuntimeError, never SDK handles or
## HRESULTs.

when not defined(navisomaWslc):
  {.error: "src/bindings/wslc_client.nim must only be imported under -d:navisomaWslc".}

import std/[os, tables, widestrs]
import ../native/wslc_raw
import ../navisoma/errors

const
  SOk = 0'i32
  WSLC_CONTAINER_NETWORKING_MODE_BRIDGED = 1.cint
  WSLC_CONTAINER_START_FLAG_ATTACH = 1.cint
  WSLC_SIGNAL_SIGTERM = 15.cint
  WSLC_SIGNAL_SIGKILL = 9.cint
  WSLC_DELETE_CONTAINER_FLAG_FORCE = 1.cint
  WSLC_E_CONTAINER_NOT_RUNNING = cast[int32](0x80040605'u32)
  WaitObject0 = 0'u32
  WaitTimeout = 258'u32
  Infinite = 0xFFFF_FFFF'u32

type
  WslcClient* = ref object
    session: WslcSession
    containers: Table[string, WslcContainer]
    storagePath: string
    comInitialized: bool

proc close*(client: WslcClient)

proc failed(hr: HResult): bool {.inline.} = hr < SOk

proc freeSdkMessage(message: pointer) =
  if not message.isNil:
    coTaskMemFree(message)

proc check(hr: HResult, operation: string, message: pointer = nil) =
  freeSdkMessage(message)
  if failed(hr):
    raise newException(RuntimeError, "WSLC " & operation & " failed")

proc toCStringArray(values: seq[string]): seq[cstring] =
  result = newSeq[cstring](values.len)
  for i, value in values:
    result[i] = value.cstring

template asArray(values: seq[cstring]): ptr UncheckedArray[cstring] =
  if values.len == 0: nil else: cast[ptr UncheckedArray[cstring]](unsafeAddr values[0])

proc newWslcClient*(projectId: string): WslcClient =
  new(result)
  let comResult = coInitializeEx(nil, 0'u32) # COINIT_MULTITHREADED
  if failed(comResult):
    raise newException(RuntimeError, "WSLC runtime is unavailable")
  result.comInitialized = true

  var missing: uint32
  try:
    check(wslcGetMissingComponents(addr missing), "preflight")
    if missing != 0:
      raise newException(RuntimeError, "WSLC runtime is unavailable")

    var version: WslcVersion
    check(wslcGetVersion(addr version), "version check")

    let base = getEnv("LOCALAPPDATA", getTempDir()) / "navisoma" / "wslc"
    result.storagePath = base / projectId
    createDir(result.storagePath)

    var settings: WslcSessionSettings
    let sessionName = newWideCString("navisoma-" & projectId)
    let storagePath = newWideCString(result.storagePath)
    check(wslcInitSessionSettings(sessionName, storagePath, addr settings), "session setup")

    var errorMessage: pointer
    check(wslcCreateSession(addr settings, addr result.session, addr errorMessage), "session creation", errorMessage)
  except CatchableError:
    result.close()
    raise

proc close*(client: WslcClient) =
  if client.isNil:
    return
  for _, container in client.containers:
    var errorMessage: pointer
    discard wslcDeleteContainer(container, WSLC_DELETE_CONTAINER_FLAG_FORCE, addr errorMessage)
    freeSdkMessage(errorMessage)
    discard wslcReleaseContainer(container)
  client.containers.clear()
  if not client.session.isNil:
    discard wslcTerminateSession(client.session)
    discard wslcReleaseSession(client.session)
    client.session = nil
  if client.comInitialized:
    coUninitialize()
    client.comInitialized = false
  if client.storagePath.len > 0 and dirExists(client.storagePath):
    try:
      removeDir(client.storagePath)
    except OSError:
      discard

proc resolveImage*(client: WslcClient, image: string): string =
  var options = WslcPullImageOptions(uri: image.cstring)
  var errorMessage: pointer
  check(wslcPullSessionImage(client.session, addr options, addr errorMessage), "image resolve", errorMessage)
  image

proc createContainer*(client: WslcClient, serviceName, image: string,
                      command: seq[string], environment: seq[tuple[key, value: string]]) =
  var settings: WslcContainerSettings
  check(wslcInitContainerSettings(image.cstring, addr settings), "container setup")
  check(wslcSetContainerSettingsName(addr settings, serviceName.cstring), "container naming")
  check(wslcSetContainerSettingsNetworkingMode(addr settings, WSLC_CONTAINER_NETWORKING_MODE_BRIDGED),
        "network setup")

  if command.len > 0 or environment.len > 0:
    var process: WslcProcessSettings
    check(wslcInitProcessSettings(addr process), "process setup")
    let argv = toCStringArray(command)
    if command.len > 0:
      check(wslcSetProcessSettingsCmdLine(addr process, asArray(argv), argv.len.csize_t), "process command")
    if environment.len > 0:
      var env = newSeq[string](environment.len)
      for i, pair in environment:
        env[i] = pair.key & "=" & pair.value
      let values = toCStringArray(env)
      check(wslcSetProcessSettingsEnvVariables(addr process, asArray(values), values.len.csize_t),
            "process environment")
    check(wslcSetContainerSettingsInitProcess(addr settings, addr process), "init process setup")

  var container: WslcContainer
  var errorMessage: pointer
  check(wslcCreateContainer(client.session, addr settings, addr container, addr errorMessage),
        "container creation", errorMessage)
  client.containers[serviceName] = container

proc containerFor(client: WslcClient, serviceName: string): WslcContainer =
  if serviceName notin client.containers:
    raise newException(RuntimeError, "WSLC container '" & serviceName & "' is not active")
  client.containers[serviceName]

proc startContainer*(client: WslcClient, serviceName: string) =
  var errorMessage: pointer
  check(wslcStartContainer(client.containerFor(serviceName), WSLC_CONTAINER_START_FLAG_ATTACH, addr errorMessage),
        "container start", errorMessage)

proc stopContainer*(client: WslcClient, serviceName: string) =
  if serviceName notin client.containers:
    return
  var errorMessage: pointer
  let hr = wslcStopContainer(client.containers[serviceName], WSLC_SIGNAL_SIGTERM, 10'u32, addr errorMessage)
  freeSdkMessage(errorMessage)
  if failed(hr) and hr != WSLC_E_CONTAINER_NOT_RUNNING:
    raise newException(RuntimeError, "WSLC container stop failed")

proc removeContainer*(client: WslcClient, serviceName: string) =
  if serviceName notin client.containers:
    return
  let container = client.containers[serviceName]
  var errorMessage: pointer
  check(wslcDeleteContainer(container, WSLC_DELETE_CONTAINER_FLAG_FORCE, addr errorMessage),
        "container deletion", errorMessage)
  check(wslcReleaseContainer(container), "container release")
  client.containers.del(serviceName)

proc execHealthProbe*(client: WslcClient, serviceName: string, test: seq[string], timeoutMs: int64): int =
  var settings: WslcProcessSettings
  check(wslcInitProcessSettings(addr settings), "health process setup")
  let argv = toCStringArray(test)
  check(wslcSetProcessSettingsCmdLine(addr settings, asArray(argv), argv.len.csize_t), "health process command")

  var process: WslcProcess
  var errorMessage: pointer
  check(wslcCreateContainerProcess(client.containerFor(serviceName), addr settings, addr process, addr errorMessage),
        "health process creation", errorMessage)
  defer: discard wslcReleaseProcess(process)

  var exitEvent: pointer
  check(wslcGetProcessExitEvent(process, addr exitEvent), "health process wait setup")
  let timeout = if timeoutMs > int64(high(uint32) - 1): high(uint32) - 1 else: timeoutMs.uint32
  let waitResult = waitForSingleObject(exitEvent, timeout)
  if waitResult == WaitTimeout:
    discard wslcSignalProcess(process, WSLC_SIGNAL_SIGKILL)
    discard waitForSingleObject(exitEvent, Infinite)
    return 124
  if waitResult != WaitObject0:
    raise newException(RuntimeError, "WSLC health process wait failed")

  var exitCode: int32
  check(wslcGetProcessExitCode(process, addr exitCode), "health process result")
  exitCode.int
