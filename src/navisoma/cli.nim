## CLI per #18's "User operations": `plan`, `up`, `down`, each taking
## `--backend <containerd|wslc>` and a Compose file path.
##
## `plan` only needs the pure parser and planner. `up`/`down` additionally
## need a real BackendPort: the containerd adapter (#18 phase 3) is wired
## in when this binary is built with -d:navisomaContainerd (see
## src/native/containerd_raw.nim's own comment on why that flag exists —
## the containerd backend needs native toolchain/headers this project
## never assumes are present on an arbitrary build machine). WSLC likewise
## compiles only under -d:navisomaWslc, where a same-user daemon owns the SDK
## session across separate `up` and `down` CLI processes.

import std/[strutils, options]
import ./compose_parser
import ./planner
import ./errors

when defined(navisomaContainerd) or defined(navisomaWslc):
  import std/[os, sha1]

  proc projectIdFor(composeFile: string): string =
    ## A stable per-compose-file id, so `up` and `down` invoked against the same file (the only
    ## thing the MVP CLI has to identify "a project" with — there is no persisted invocation
    ## state) always agree on it, and two different compose files never agree on it. Used only to
    ## namespace backend-native container/task ids (containerd_backend.nim) before it ever reaches
    ## the native boundary, and to address WSLC's same-user daemon.
    let canonical =
      try: expandFilename(composeFile)
      except OSError: composeFile
    ($secureHash(canonical))[0 ..< 12].toLowerAscii()

when defined(navisomaContainerd):
  import ./types
  import ./executor
  import ./backend
  import ./backends/containerd_backend

  proc readProjectOrFail(file: string): ComposeProject =
    let source =
      try:
        readFile(file)
      except IOError as e:
        raise newException(NavisomaError, "cannot read '" & file & "': " & e.msg)
    parseComposeProject(source)

when defined(navisomaWslc):
  import ./wslc_daemon

type
  Backend* = enum
    backendContainerd, backendWslc

proc parseBackend(raw: string): Backend =
  case raw
  of "containerd": backendContainerd
  of "wslc": backendWslc
  else:
    stderr.writeLine("error: --backend must be 'containerd' or 'wslc', got '" & raw & "'")
    quit(1)

proc parseArgs(args: seq[string]): tuple[backend: Option[string], file: Option[string]] =
  var i = 0
  while i < args.len:
    if args[i] == "--backend" and i + 1 < args.len:
      result.backend = some(args[i + 1])
      inc i, 2
    elif args[i].startsWith("--backend="):
      result.backend = some(args[i]["--backend=".len .. ^1])
      inc i
    else:
      result.file = some(args[i])
      inc i

proc cmdPlan(args: seq[string]): int =
  let parsed = parseArgs(args)
  if parsed.backend.isNone or parsed.file.isNone:
    stderr.writeLine("usage: navisoma plan --backend <containerd|wslc> <compose.yaml>")
    return 1
  discard parseBackend(parsed.backend.get())
  let source =
    try:
      readFile(parsed.file.get())
    except IOError as e:
      stderr.writeLine("error: cannot read '" & parsed.file.get() & "': " & e.msg)
      return 1
  try:
    let project = parseComposeProject(source)
    let trace = planUp(project)
    for action in trace:
      echo $action
    return 0
  except NavisomaError as e:
    stderr.writeLine("error: " & e.msg)
    return 1

proc cmdUp(args: seq[string]): int =
  let parsed = parseArgs(args)
  if parsed.backend.isNone or parsed.file.isNone:
    stderr.writeLine("usage: navisoma up --backend <containerd|wslc> <compose.yaml>")
    return 1
  let backend = parseBackend(parsed.backend.get())
  try:
    case backend
    of backendContainerd:
      when defined(navisomaContainerd):
        let project = readProjectOrFail(parsed.file.get())
        let (port, client) = newContainerdPort(projectIdFor(parsed.file.get()))
        defer: client.close()
        discard runUp(project, port, newRealProbeClock())
        return 0
      else:
        stderr.writeLine("error: this build has no containerd backend compiled in " &
          "(build with -d:navisomaContainerd)")
        return 1
    of backendWslc:
      when defined(navisomaWslc):
        runWslcUp(projectIdFor(parsed.file.get()), parsed.file.get())
        return 0
      else:
        stderr.writeLine("error: this build has no wslc backend compiled in " &
          "(build with -d:navisomaWslc)")
        return 1
  except NavisomaError as e:
    stderr.writeLine("error: " & e.msg)
    return 1

proc cmdDown(args: seq[string]): int =
  let parsed = parseArgs(args)
  if parsed.backend.isNone or parsed.file.isNone:
    stderr.writeLine("usage: navisoma down --backend <containerd|wslc> <compose.yaml>")
    return 1
  let backend = parseBackend(parsed.backend.get())
  try:
    case backend
    of backendContainerd:
      when defined(navisomaContainerd):
        let project = readProjectOrFail(parsed.file.get())
        let (port, client) = newContainerdPort(projectIdFor(parsed.file.get()))
        defer: client.close()
        # Deterministically reverse-order stop/remove exactly the services `up` would have
        # created for this fixture (planDown's own contract) — idempotent per service even if
        # some were never actually running, since the adapter's stop/remove tolerate "not found".
        for action in planDown(planUp(project)):
          case action.kind
          of akStopContainer: port.stopContainer(action.service)
          of akRemoveContainer: port.removeContainer(action.service)
          else: discard
        return 0
      else:
        stderr.writeLine("error: this build has no containerd backend compiled in " &
          "(build with -d:navisomaContainerd)")
        return 1
    of backendWslc:
      when defined(navisomaWslc):
        runWslcDown(projectIdFor(parsed.file.get()))
        return 0
      else:
        stderr.writeLine("error: this build has no wslc backend compiled in " &
          "(build with -d:navisomaWslc)")
        return 1
  except NavisomaError as e:
    stderr.writeLine("error: " & e.msg)
    return 1

proc runCli*(args: seq[string]): int =
  when defined(navisomaWslc):
    if args.len > 0 and args[0] == "--wslc-daemon":
      return runWslcDaemon(args[1 .. ^1])
  if args.len == 0:
    stderr.writeLine("usage: navisoma <plan|up|down> --backend <containerd|wslc> <compose.yaml>")
    return 1
  case args[0]
  of "plan": cmdPlan(args[1 .. ^1])
  of "up": cmdUp(args[1 .. ^1])
  of "down": cmdDown(args[1 .. ^1])
  else:
    stderr.writeLine("error: unknown command '" & args[0] & "'")
    1
