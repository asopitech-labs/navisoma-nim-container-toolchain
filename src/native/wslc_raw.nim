## Direct bindings to the pinned WSLC SDK C projection. The opaque settings
## objects deliberately use the SDK header's published size/alignment values;
## the build container extracts and hash-checks that exact header before Nim
## compiles this module.

when not defined(navisomaWslc):
  {.error: "src/native/wslc_raw.nim must only be imported under -d:navisomaWslc".}

when not defined(windows):
  {.error: "-d:navisomaWslc requires a Windows target".}

import std/widestrs

const
  wslcSdkRoot = "/opt/navisoma-wslc-sdk"

{.passC: "-target x86_64-windows-gnu".}
{.passC: "-I" & wslcSdkRoot & "/include".}
## The SDK header expects these WDK macros. They are compatibility shims for
## Zig/MinGW only; wslcsdk.h itself remains the byte-for-byte pinned vendor file.
{.passC: "-DEXTERN_C_START= -DEXTERN_C_END= -D__callback=".}
{.passL: "-target x86_64-windows-gnu".}
{.passL: wslcSdkRoot & "/runtimes/win-x64/wslcsdk.lib".}
{.passL: "-lole32 -lshell32 -lws2_32".}

type
  HResult* = int32
  WslcSession* {.importc: "WslcSession", header: "wslcsdk.h".} = pointer
  WslcContainer* {.importc: "WslcContainer", header: "wslcsdk.h".} = pointer
  WslcProcess* {.importc: "WslcProcess", header: "wslcsdk.h".} = pointer

  WslcSessionSettings* {.importc: "WslcSessionSettings", header: "wslcsdk.h", bycopy, completeStruct.} = object
    opaque: array[9, uint64]
  WslcContainerSettings* {.importc: "WslcContainerSettings", header: "wslcsdk.h", bycopy, completeStruct.} = object
    opaque: array[13, uint64]
  WslcProcessSettings* {.importc: "WslcProcessSettings", header: "wslcsdk.h", bycopy, completeStruct.} = object
    opaque: array[9, uint64]
  WslcPullImageOptions* {.importc: "WslcPullImageOptions", header: "wslcsdk.h", bycopy, completeStruct.} = object
    uri*: cstring
    progressCallback: pointer
    progressCallbackContext: pointer
    registryAuth: cstring
  WslcVersion* {.importc: "WslcVersion", header: "wslcsdk.h", bycopy, completeStruct.} = object
    major*, minor*, revision*: uint32

static:
  doAssert sizeof(WslcSessionSettings) == 72
  doAssert alignof(WslcSessionSettings) == 8
  doAssert sizeof(WslcContainerSettings) == 104
  doAssert alignof(WslcContainerSettings) == 8
  doAssert sizeof(WslcProcessSettings) == 72
  doAssert alignof(WslcProcessSettings) == 8

proc coInitializeEx*(reserved: pointer, flags: uint32): HResult
  {.stdcall, importc: "CoInitializeEx", dynlib: "ole32.dll".}
proc coUninitialize*()
  {.stdcall, importc: "CoUninitialize", dynlib: "ole32.dll".}
proc coTaskMemFree*(memory: pointer)
  {.stdcall, importc: "CoTaskMemFree", dynlib: "ole32.dll".}
proc waitForSingleObject*(handle: pointer, milliseconds: uint32): uint32
  {.stdcall, importc: "WaitForSingleObject", dynlib: "kernel32.dll".}

proc wslcGetMissingComponents*(missing: ptr uint32): HResult
  {.stdcall, importc: "WslcGetMissingComponents", header: "wslcsdk.h".}
proc wslcGetVersion*(version: ptr WslcVersion): HResult
  {.stdcall, importc: "WslcGetVersion", header: "wslcsdk.h".}
proc wslcInitSessionSettings*(name, storagePath: WideCString,
                              settings: ptr WslcSessionSettings): HResult
  {.stdcall, importc: "WslcInitSessionSettings", header: "wslcsdk.h".}
proc wslcCreateSession*(settings: ptr WslcSessionSettings, session: ptr WslcSession,
                        errorMessage: ptr pointer): HResult
  {.stdcall, importc: "WslcCreateSession", header: "wslcsdk.h".}
proc wslcTerminateSession*(session: WslcSession): HResult
  {.stdcall, importc: "WslcTerminateSession", header: "wslcsdk.h".}
proc wslcReleaseSession*(session: WslcSession): HResult
  {.stdcall, importc: "WslcReleaseSession", header: "wslcsdk.h".}

proc wslcPullSessionImage*(session: WslcSession, options: ptr WslcPullImageOptions,
                           errorMessage: ptr pointer): HResult
  {.stdcall, importc: "WslcPullSessionImage", header: "wslcsdk.h".}
proc wslcInitContainerSettings*(image: cstring, settings: ptr WslcContainerSettings): HResult
  {.stdcall, importc: "WslcInitContainerSettings", header: "wslcsdk.h".}
proc wslcSetContainerSettingsName*(settings: ptr WslcContainerSettings, name: cstring): HResult
  {.stdcall, importc: "WslcSetContainerSettingsName", header: "wslcsdk.h".}
proc wslcSetContainerSettingsNetworkingMode*(settings: ptr WslcContainerSettings,
                                             mode: cint): HResult
  {.stdcall, importc: "WslcSetContainerSettingsNetworkingMode", header: "wslcsdk.h".}
proc wslcSetContainerSettingsInitProcess*(settings: ptr WslcContainerSettings,
                                          process: ptr WslcProcessSettings): HResult
  {.stdcall, importc: "WslcSetContainerSettingsInitProcess", header: "wslcsdk.h".}
proc wslcCreateContainer*(session: WslcSession, settings: ptr WslcContainerSettings,
                          container: ptr WslcContainer, errorMessage: ptr pointer): HResult
  {.stdcall, importc: "WslcCreateContainer", header: "wslcsdk.h".}
proc wslcStartContainer*(container: WslcContainer, flags: cint,
                         errorMessage: ptr pointer): HResult
  {.stdcall, importc: "WslcStartContainer", header: "wslcsdk.h".}
proc wslcStopContainer*(container: WslcContainer, signal: cint, timeoutSeconds: uint32,
                        errorMessage: ptr pointer): HResult
  {.stdcall, importc: "WslcStopContainer", header: "wslcsdk.h".}
proc wslcDeleteContainer*(container: WslcContainer, flags: cint,
                          errorMessage: ptr pointer): HResult
  {.stdcall, importc: "WslcDeleteContainer", header: "wslcsdk.h".}
proc wslcReleaseContainer*(container: WslcContainer): HResult
  {.stdcall, importc: "WslcReleaseContainer", header: "wslcsdk.h".}

proc wslcInitProcessSettings*(settings: ptr WslcProcessSettings): HResult
  {.stdcall, importc: "WslcInitProcessSettings", header: "wslcsdk.h".}
proc wslcSetProcessSettingsCmdLine*(settings: ptr WslcProcessSettings,
                                    argv: ptr UncheckedArray[cstring], argc: csize_t): HResult
  {.stdcall, importc: "WslcSetProcessSettingsCmdLine", header: "wslcsdk.h".}
proc wslcSetProcessSettingsEnvVariables*(settings: ptr WslcProcessSettings,
                                         environment: ptr UncheckedArray[cstring], count: csize_t): HResult
  {.stdcall, importc: "WslcSetProcessSettingsEnvVariables", header: "wslcsdk.h".}
proc wslcCreateContainerProcess*(container: WslcContainer, settings: ptr WslcProcessSettings,
                                 process: ptr WslcProcess, errorMessage: ptr pointer): HResult
  {.stdcall, importc: "WslcCreateContainerProcess", header: "wslcsdk.h".}
proc wslcGetProcessExitEvent*(process: WslcProcess, event: ptr pointer): HResult
  {.stdcall, importc: "WslcGetProcessExitEvent", header: "wslcsdk.h".}
proc wslcGetProcessExitCode*(process: WslcProcess, exitCode: ptr int32): HResult
  {.stdcall, importc: "WslcGetProcessExitCode", header: "wslcsdk.h".}
proc wslcSignalProcess*(process: WslcProcess, signal: cint): HResult
  {.stdcall, importc: "WslcSignalProcess", header: "wslcsdk.h".}
proc wslcReleaseProcess*(process: WslcProcess): HResult
  {.stdcall, importc: "WslcReleaseProcess", header: "wslcsdk.h".}
