## Deterministic fake `BackendPort` for #18 phase 2 executor scenario
## tests only. Not shipped from src/navisoma, and the CLI has no
## `--backend fake` option: this fake is planner/executor testing
## infrastructure, never a claim that any real backend works this way.
##
## No `sleep`, no real time, no environment variable, no global state
## (#18 phase 2 timing rule): every call is answered from state held in
## one `FakeBackend` instance, and every probe outcome is pre-scripted
## by the test that builds it.

import std/[tables, times]
import navisoma/types
import navisoma/backend
import navisoma/errors

type
  CallKind* = enum
    ckResolveImage, ckCreateContainer, ckStartContainer, ckStopContainer,
    ckRemoveContainer, ckExecHealthProbe

  Call* = object
    kind*: CallKind
    arg*: string
      ## The image reference for `ckResolveImage`; the service name for
      ## every other call kind.

  FakeBackend* = ref object
    calls: seq[Call]
    probeExitCodes: Table[string, seq[int]]
    failing: seq[tuple[kind: CallKind, service: string]]

proc newFakeBackend*(): FakeBackend =
  FakeBackend()

proc calls*(fb: FakeBackend): seq[Call] = fb.calls

proc scriptProbes*(fb: FakeBackend, service: string, exitCodes: seq[int]) =
  ## Health-probe exit codes for `service`, consumed in order, one per
  ## `execHealthProbe` call — the "scripted probe outcome" the timing
  ## rule calls for instead of a real health command.
  fb.probeExitCodes[service] = exitCodes

proc failOn*(fb: FakeBackend, kind: CallKind, service: string) =
  ## Make the given call raise `RuntimeError` for `service` instead of
  ## succeeding, to exercise the executor's backend-failure cleanup.
  fb.failing.add (kind, service)

proc shouldFail(fb: FakeBackend, kind: CallKind, service: string): bool =
  for f in fb.failing:
    if f.kind == kind and f.service == service:
      return true
  false

proc port*(fb: FakeBackend): BackendPort =
  result.resolveImage = proc (image: string): ResolvedImage =
    fb.calls.add Call(kind: ckResolveImage, arg: image)
    ResolvedImage(id: image)

  result.createContainer = proc (service: ServiceSpec, image: ResolvedImage) =
    fb.calls.add Call(kind: ckCreateContainer, arg: service.name)
    if fb.shouldFail(ckCreateContainer, service.name):
      raise newException(RuntimeError, "fake backend: createContainer failed for " & service.name)

  result.startContainer = proc (service: string) =
    fb.calls.add Call(kind: ckStartContainer, arg: service)
    if fb.shouldFail(ckStartContainer, service):
      raise newException(RuntimeError, "fake backend: startContainer failed for " & service)

  result.stopContainer = proc (service: string) =
    fb.calls.add Call(kind: ckStopContainer, arg: service)

  result.removeContainer = proc (service: string) =
    fb.calls.add Call(kind: ckRemoveContainer, arg: service)

  result.execHealthProbe = proc (service: string, test: seq[string], timeout: Duration): ProbeResult =
    fb.calls.add Call(kind: ckExecHealthProbe, arg: service)
    if fb.shouldFail(ckExecHealthProbe, service):
      raise newException(RuntimeError, "fake backend: execHealthProbe failed for " & service)
    doAssert service in fb.probeExitCodes and fb.probeExitCodes[service].len > 0,
      "fake backend: no scripted probe outcome left for " & service
    let code = fb.probeExitCodes[service][0]
    fb.probeExitCodes[service].delete(0)
    ProbeResult(exitCode: code)
