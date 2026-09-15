## The executor (#18 phase 2): runs a `planUp` trace against a
## `BackendPort`, owning action order, health-probe repetition, failure
## handling, and reverse cleanup — the planner stays pure (action trace
## and health-state derivation only; see planner.nim and health.nim).
##
## No backend-native handle crosses this boundary: every backend call is
## addressed by a NAVISOMA-level identifier (image reference, service
## name), per backend.nim.

import std/[tables, times, options]
import ./types
import ./planner
import ./health
import ./backend
import ./errors

type
  ProbeClock* = proc (attempt: int): Duration {.closure.}
    ## Returns "elapsed time since the container was judged startable"
    ## for the `attempt`-th (0-based) health probe within one
    ## `AwaitHealth` action. Callers (tests, and later a real adapter)
    ## supply this directly — the executor itself never sleeps, reads
    ## the real clock, an environment variable, or any global state
    ## (#18 phase 2 timing rule).

proc newRealProbeClock*(): ProbeClock =
  ## A real wall-clock `ProbeClock` for a real (non-fake) backend adapter. The closure has no
  ## signal for "a new service's health-awaiting has begun" other than `attempt` resetting to
  ## 0 (the executor always starts each service's `AwaitHealth` loop at attempt 0), so it treats
  ## that as the reference point and returns elapsed wall-clock time relative to it thereafter.
  var start: Time
  result = proc (attempt: int): Duration =
    let now = getTime()
    if attempt == 0:
      start = now
    now - start

proc cleanup(port: BackendPort, journal: seq[string]) =
  ## Reverse-order stop/remove of exactly the services this invocation
  ## actually created, per the journal passed in — never the full plan,
  ## and never twice for the same service (#18 phase 2 journal rule).
  for i in countdown(journal.len - 1, 0):
    port.stopContainer(journal[i])
    port.removeContainer(journal[i])

proc runUp*(project: ComposeProject, port: BackendPort, clock: ProbeClock): seq[string] =
  ## Executes `planUp(project)` against `port` in order. Returns the
  ## invocation journal (services successfully created) on success.
  ##
  ## On any failure — a dependency reaching `unhealthy` after retries,
  ## or a backend error raised from resolve/create/start/exec — cleans
  ## up exactly the journaled resources in reverse order, then
  ## re-raises the original error unchanged.
  let trace = planUp(project)
  var journal: seq[string]
  var resolved = initTable[string, ResolvedImage]()
  try:
    for action in trace:
      case action.kind
      of akResolveImage:
        let svc = project.findService(action.service).get()
        resolved[action.service] = port.resolveImage(svc.image)
      of akCreateContainer:
        let svc = project.findService(action.service).get()
        port.createContainer(svc, resolved[action.service])
        journal.add action.service
      of akStartContainer:
        port.startContainer(action.service)
      of akAwaitHealth:
        let svc = project.findService(action.service).get()
        let spec = svc.healthcheck.get()
        var state = initHealthState()
        var attempt = 0
        while state.phase == hpStarting:
          let probe = port.execHealthProbe(action.service, spec.test)
          let outcome = if probe.exitCode == 0: probeSuccess else: probeFailure
          state = stepHealth(spec, state, outcome, clock(attempt))
          inc attempt
        if state.phase == hpUnhealthy:
          raise newException(UnhealthyError,
            "service '" & action.service & "' is unhealthy after " & $spec.retries & " retries")
      of akStopContainer, akRemoveContainer:
        discard # planUp never emits these; cleanup below is journal-driven, not trace-driven.
  except CatchableError:
    cleanup(port, journal)
    raise
  journal
