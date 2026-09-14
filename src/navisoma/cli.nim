## CLI skeleton per #18's "User operations": `plan`, `up`, `down`, each
## taking `--backend <containerd|wslc>` and a Compose file path.
##
## Phase 1 (this commit) has no backend port or executor yet (#18 phases
## 2-4), so `plan` is fully functional — it only needs the pure parser and
## planner — while `up`/`down` report themselves unimplemented and exit
## non-zero rather than silently doing nothing or pretending to succeed.

import std/[strutils, options]
import ./compose_parser
import ./planner
import ./errors

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

proc cmdUnimplemented(command: string): int =
  stderr.writeLine("error: 'navisoma " & command &
    "' has no backend executor yet (#18 phases 2-4 are not implemented)")
  1

proc runCli*(args: seq[string]): int =
  if args.len == 0:
    stderr.writeLine("usage: navisoma <plan|up|down> --backend <containerd|wslc> <compose.yaml>")
    return 1
  case args[0]
  of "plan": cmdPlan(args[1 .. ^1])
  of "up": cmdUnimplemented("up")
  of "down": cmdUnimplemented("down")
  else:
    stderr.writeLine("error: unknown command '" & args[0] & "'")
    1
