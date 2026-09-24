## The WSLC SDK has no cross-process session re-attach. This is the narrow,
## same-user named-pipe control path between the ordinary CLI process and the
## daemon that owns the SDK handles.

when not defined(navisomaWslc):
  {.error: "src/navisoma/wslc_pipe.nim must only be imported under -d:navisomaWslc".}

import std/widestrs
import ./errors

const
  GenericRead = 0x8000_0000'u32
  GenericWrite = 0x4000_0000'u32
  OpenExisting = 3'u32
  PipeAccessDuplex = 0x0000_0003'u32
  PipeTypeMessage = 0x0000_0004'u32
  PipeReadModeMessage = 0x0000_0002'u32
  ErrorPipeConnected = 535'u32
  InvalidHandleValue = cast[pointer](-1'i64)

type
  SecurityAttributes {.bycopy.} = object
    nLength: uint32
    securityDescriptor: pointer
    inheritHandle: int32

proc convertStringSecurityDescriptorToSecurityDescriptorW*(source: WideCString,
    revision: uint32, descriptor: ptr pointer, descriptorSize: ptr uint32): int32
  {.stdcall, importc: "ConvertStringSecurityDescriptorToSecurityDescriptorW", dynlib: "advapi32.dll".}
proc localFree(memory: pointer): pointer
  {.stdcall, importc: "LocalFree", dynlib: "kernel32.dll".}
proc createNamedPipeW(name: WideCString, openMode, pipeMode, maxInstances,
    outBufferSize, inBufferSize, defaultTimeout: uint32, security: ptr SecurityAttributes): pointer
  {.stdcall, importc: "CreateNamedPipeW", dynlib: "kernel32.dll".}
proc connectNamedPipe(pipe: pointer, overlapped: pointer): int32
  {.stdcall, importc: "ConnectNamedPipe", dynlib: "kernel32.dll".}
proc disconnectNamedPipe(pipe: pointer): int32
  {.stdcall, importc: "DisconnectNamedPipe", dynlib: "kernel32.dll".}
proc createFileW(name: WideCString, desiredAccess, shareMode: uint32,
    security: ptr SecurityAttributes, creationDisposition, flagsAndAttributes: uint32,
    templateFile: pointer): pointer
  {.stdcall, importc: "CreateFileW", dynlib: "kernel32.dll".}
proc readFile(handle, buffer: pointer, bytesToRead: uint32, bytesRead: ptr uint32,
    overlapped: pointer): int32
  {.stdcall, importc: "ReadFile", dynlib: "kernel32.dll".}
proc writeFile(handle, buffer: pointer, bytesToWrite: uint32, bytesWritten: ptr uint32,
    overlapped: pointer): int32
  {.stdcall, importc: "WriteFile", dynlib: "kernel32.dll".}
proc flushFileBuffers(handle: pointer): int32
  {.stdcall, importc: "FlushFileBuffers", dynlib: "kernel32.dll".}
proc closeHandle(handle: pointer): int32
  {.stdcall, importc: "CloseHandle", dynlib: "kernel32.dll".}
proc getLastError(): uint32
  {.stdcall, importc: "GetLastError", dynlib: "kernel32.dll".}

proc isInvalid(handle: pointer): bool {.inline.} = handle.isNil or handle == InvalidHandleValue

proc openServerPipe*(name: string): pointer =
  ## Owner-only access keeps another local account from issuing lifecycle
  ## commands to a session it does not own.
  let sddl = newWideCString("D:P(A;;GA;;;OW)")
  var descriptor: pointer
  if convertStringSecurityDescriptorToSecurityDescriptorW(sddl, 1'u32, addr descriptor, nil) == 0:
    raise newException(RuntimeError, "WSLC control pipe security setup failed")
  defer: discard localFree(descriptor)

  var security = SecurityAttributes(nLength: sizeof(SecurityAttributes).uint32,
                                    securityDescriptor: descriptor, inheritHandle: 0)
  let pipe = createNamedPipeW(newWideCString(name), PipeAccessDuplex,
                              PipeTypeMessage or PipeReadModeMessage, 1'u32,
                              4096'u32, 4096'u32, 0'u32, addr security)
  if isInvalid(pipe):
    raise newException(RuntimeError, "WSLC control pipe creation failed")
  pipe

proc acceptPipeClient*(pipe: pointer) =
  if connectNamedPipe(pipe, nil) == 0 and getLastError() != ErrorPipeConnected:
    raise newException(RuntimeError, "WSLC control pipe connection failed")

proc readPipeCommand*(pipe: pointer): string =
  result = newString(4096)
  var bytesRead: uint32
  if readFile(pipe, unsafeAddr result[0], result.len.uint32, addr bytesRead, nil) == 0:
    raise newException(RuntimeError, "WSLC control pipe read failed")
  result.setLen(bytesRead.int)

proc writePipeResponse*(pipe: pointer, response: string) =
  var bytesWritten: uint32
  if writeFile(pipe, unsafeAddr response[0], response.len.uint32, addr bytesWritten, nil) == 0 or
      bytesWritten != response.len.uint32:
    raise newException(RuntimeError, "WSLC control pipe write failed")
  discard flushFileBuffers(pipe)

proc closeServerPipe*(pipe: pointer) =
  discard disconnectNamedPipe(pipe)
  discard closeHandle(pipe)

proc tryOpenPipe(name: string): pointer =
  let pipe = createFileW(newWideCString(name), GenericRead or GenericWrite, 0'u32, nil,
                         OpenExisting, 0'u32, nil)
  if isInvalid(pipe): nil else: pipe

proc sendPipeCommand*(name, command: string): string =
  let pipe = tryOpenPipe(name)
  if pipe.isNil:
    raise newException(RuntimeError, "WSLC session is not active")
  defer: discard closeHandle(pipe)

  var bytesWritten: uint32
  if writeFile(pipe, unsafeAddr command[0], command.len.uint32, addr bytesWritten, nil) == 0 or
      bytesWritten != command.len.uint32:
    raise newException(RuntimeError, "WSLC control pipe write failed")

  result = newString(4096)
  var bytesRead: uint32
  if readFile(pipe, unsafeAddr result[0], result.len.uint32, addr bytesRead, nil) == 0:
    raise newException(RuntimeError, "WSLC control pipe read failed")
  result.setLen(bytesRead.int)
