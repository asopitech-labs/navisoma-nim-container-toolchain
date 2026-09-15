## Deterministic planner: canonical project -> a stable action trace, per
## docs/architecture.md section 3 ("Execution graph"). Pure and
## backend-neutral: no backend port exists yet (that is #18 phase 2).

import std/[tables, options, strutils, sugar]
import ./types
import ./errors

type
  ActionKind* = enum
    akResolveImage, akCreateContainer, akStartContainer, akAwaitHealth,
    akStopContainer, akRemoveContainer

  Action* = object
    kind*: ActionKind
    service*: string

const ActionDisplayName: array[ActionKind, string] = [
  akResolveImage: "ResolveImage", akCreateContainer: "CreateContainer",
  akStartContainer: "StartContainer", akAwaitHealth: "AwaitHealth",
  akStopContainer: "StopContainer", akRemoveContainer: "RemoveContainer"]

proc `$`*(a: Action): string =
  ## Matches docs/architecture.md section 3's execution-graph vocabulary
  ## exactly, so `navisoma plan` output uses the same names as the design.
  ActionDisplayName[a.kind] & "(" & a.service & ")"

proc validate(project: ComposeProject) =
  ## Structural checks over the `depends_on` graph. Each check runs to
  ## completion and reports every violation it finds, rather than stopping
  ## at the first one, so a caller sees the whole rejection reason at once.
  var unknownRefs: seq[string]
  var missingHealthcheckRefs: seq[string]
  for svc in project.services:
    for edge in svc.dependsOn:
      if not project.hasService(edge.service):
        unknownRefs.add(svc.name & " -> " & edge.service)
      elif project.findService(edge.service).get().healthcheck.isNone:
        # `condition: service_healthy` is the only depends_on condition
        # this MVP boundary supports (types.nim), so every edge implies a
        # health gate; a target with no healthcheck can never satisfy it.
        missingHealthcheckRefs.add(svc.name & " -> " & edge.service)
  if unknownRefs.len > 0:
    raise newException(ComposeSemanticError,
      "depends_on references unknown service(s): " & unknownRefs.join(", "))
  if missingHealthcheckRefs.len > 0:
    raise newException(ComposeSemanticError,
      "depends_on condition service_healthy requires the target service to " &
      "declare a healthcheck: " & missingHealthcheckRefs.join(", "))

proc topoOrder(project: ComposeProject): seq[string] =
  ## Kahn's algorithm, seeded and tie-broken strictly by declaration order,
  ## so the same fixture always yields the same order (#18 acceptance
  ## criteria). Raises PlanningError if the depends_on graph has a cycle.
  var indegree = initOrderedTable[string, int]()
  var dependents = initOrderedTable[string, seq[string]]()
  for svc in project.services:
    indegree[svc.name] = 0
    dependents[svc.name] = @[]
  for svc in project.services:
    for edge in svc.dependsOn:
      inc indegree[svc.name]
      dependents[edge.service].add(svc.name)

  var ready: seq[string]
  for svc in project.services:
    if indegree[svc.name] == 0:
      ready.add(svc.name)

  result = @[]
  while ready.len > 0:
    let next = ready[0]
    ready.delete(0)
    result.add(next)
    for dependent in dependents[next]:
      dec indegree[dependent]
      if indegree[dependent] == 0:
        ready.add(dependent)

  if result.len != project.services.len:
    let unresolved = collect:
      for svc in project.services:
        if svc.name notin result: svc.name
    raise newException(PlanningError,
      "depends_on has a cycle involving: " & unresolved.join(", "))

proc planUp*(project: ComposeProject): seq[Action] =
  ## `resolve -> create -> start -> [await health]`, in dependency order,
  ## for every service. A dependency's `AwaitHealth` action is always
  ## ordered before any service that depends on it, which is what makes
  ## the gate in #18's semantic failure contract observable in the trace.
  validate(project)
  let order = topoOrder(project)
  for name in order:
    let svc = project.findService(name).get()
    result.add Action(kind: akResolveImage, service: name)
    result.add Action(kind: akCreateContainer, service: name)
    result.add Action(kind: akStartContainer, service: name)
    if svc.healthcheck.isSome:
      result.add Action(kind: akAwaitHealth, service: name)

proc planDown*(upTrace: seq[Action]): seq[Action] =
  ## Deterministic reverse-order stop/remove of exactly the services an
  ## earlier `planUp` created — #18's cleanup contract: only resources
  ## created by this invocation, in reverse order.
  var created: seq[string]
  for action in upTrace:
    if action.kind == akCreateContainer:
      created.add(action.service)
  for i in countdown(created.len - 1, 0):
    result.add Action(kind: akStopContainer, service: created[i])
    result.add Action(kind: akRemoveContainer, service: created[i])
