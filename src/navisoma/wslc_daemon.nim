## Per-project WSLC session owner. The SDK cannot re-attach a session from a
## later CLI process, so `up` starts this small daemon and `down` asks the same
## process to perform reverse-order teardown before it exits.

when not defined(navisomaWslc):
  {.error: "src/navisoma/wslc_daemon.nim must only be imported under -d:navisomaWslc".}

import std/[os, osproc, strutils]
import ./types
import ./errors
import ./compose_parser
import ./planner
import ./executor
import ./backend
import ./wslc_names
import ./wslc_pipe
import ./backends/wslc_backend

proc projectFromFile(path: string): ComposeProject =
  try:
    parseComposeProject(readFile(path))
  except IOError as e:
    raise newException(NavisomaError, "cannot read '" & path & "': " & e.msg)

proc responseOrRaise(response: string) =
  if response == "OK":
    return
  let detail = if response.startsWith("ERROR "): response["ERROR ".len .. ^1] else: "WSLC daemon failed"
  raise newException(RuntimeError, detail)

proc startDaemon(projectId, composeFile: string) =
  let process = startProcess(getAppFilename(), args = @["--wslc-daemon", projectId, composeFile],
                             options = {poUsePath, poDaemon, poParentStreams})
  close(process)

proc sendWhenReady(pipeName, command: string): string =
  for _ in 0 ..< 100:
    try:
      return sendPipeCommand(pipeName, command)
    except RuntimeError as e:
      if e.msg != "WSLC session is not active":
        raise
      sleep(100)
  raise newException(RuntimeError, "WSLC daemon did not start")

proc runWslcUp*(projectId, composeFile: string) =
  let pipeName = wslcPipeName(projectId)
  try:
    responseOrRaise(sendPipeCommand(pipeName, "up"))
    return
  except RuntimeError as e:
    if e.msg != "WSLC session is not active":
      raise

  startDaemon(projectId, composeFile)
  responseOrRaise(sendWhenReady(pipeName, "up"))

proc runWslcDown*(projectId: string) =
  responseOrRaise(sendPipeCommand(wslcPipeName(projectId), "down"))

proc daemonError(pipe: pointer, error: ref CatchableError) =
  try:
    writePipeResponse(pipe, "ERROR " & error.msg)
  except CatchableError:
    discard

proc runWslcDaemon*(args: seq[string]): int =
  if args.len != 2:
    return 1
  let (projectId, composeFile) = (args[0], args[1])
  let pipeName = wslcPipeName(projectId)
  var project: ComposeProject
  var port: BackendPort
  var client: WslcClient
  var active = false
  var exitAfterReply = false

  while not exitAfterReply:
    let pipe = openServerPipe(pipeName)
    try:
      acceptPipeClient(pipe)
      let command = readPipeCommand(pipe)
      case command
      of "up":
        if active:
          raise newException(RuntimeError, "WSLC project is already active")
        project = projectFromFile(composeFile)
        (port, client) = newWslcPort(projectId)
        try:
          discard runUp(project, port, newRealProbeClock())
          active = true
          writePipeResponse(pipe, "OK")
        except CatchableError:
          client.close()
          client = nil
          raise
      of "down":
        if not active:
          raise newException(RuntimeError, "WSLC project is not active")
        try:
          for action in planDown(planUp(project)):
            case action.kind
            of akStopContainer: port.stopContainer(action.service)
            of akRemoveContainer: port.removeContainer(action.service)
            else: discard
        finally:
          client.close()
          client = nil
          active = false
        writePipeResponse(pipe, "OK")
        exitAfterReply = true
      else:
        raise newException(RuntimeError, "invalid WSLC daemon command")
    except CatchableError as e:
      daemonError(pipe, e)
      if not active:
        exitAfterReply = true
    finally:
      closeServerPipe(pipe)

  if not client.isNil:
    client.close()
  result = 0
