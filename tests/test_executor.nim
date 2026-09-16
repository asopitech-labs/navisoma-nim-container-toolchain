## Fixture-driven scenario tests for the executor (src/navisoma/executor.nim),
## per #18 phase 2's required cases: healthy, unhealthy, backend failure,
## and journal-driven (not plan-driven) cleanup. Each uses the
## deterministic fake backend (fakebackend.nim, tests-only) and a fake
## clock — no sleep, no real time, no environment variable, no global
## state.

import std/[unittest, options, sequtils, times]
import navisoma/types
import navisoma/errors
import navisoma/executor
import ./fakebackend

proc svc(name: string, healthcheck = false, dependsOn: seq[string] = @[]): ServiceSpec =
  result = ServiceSpec(name: name, image: "registry.example.com/" & name & ":1")
  if healthcheck:
    result.healthcheck = some(HealthCheckSpec(retries: 3, interval: initDuration(seconds = 5),
                                                timeout: initDuration(seconds = 3), startPeriod: initDuration()))
  for dep in dependsOn:
    result.dependsOn.add DependsOnEdge(service: dep, condition: conditionServiceHealthy)

proc pastStartPeriodClock(attempt: int, interval: Duration): Duration = initDuration(seconds = 100)

suite "executor":
  test "healthy: db is created/started, health probed, then api is created/started":
    let project = ComposeProject(services: @[
      svc("api", dependsOn = @["db"]),
      svc("db", healthcheck = true)
    ])
    let fb = newFakeBackend()
    fb.scriptProbes("db", @[0])

    let journal = runUp(project, fb.port(), pastStartPeriodClock)

    check journal == @["db", "api"]
    check fb.calls == @[
      Call(kind: ckResolveImage, arg: "registry.example.com/db:1"),
      Call(kind: ckCreateContainer, arg: "db"),
      Call(kind: ckStartContainer, arg: "db"),
      Call(kind: ckExecHealthProbe, arg: "db"),
      Call(kind: ckResolveImage, arg: "registry.example.com/api:1"),
      Call(kind: ckCreateContainer, arg: "api"),
      Call(kind: ckStartContainer, arg: "api"),
    ]

  test "unhealthy: retries exhausted means api is never created/started, and only db is cleaned up":
    let project = ComposeProject(services: @[
      svc("api", dependsOn = @["db"]),
      svc("db", healthcheck = true)
    ])
    let fb = newFakeBackend()
    fb.scriptProbes("db", @[1, 1, 1]) # 3 failures == retries, start_period already elapsed

    expect UnhealthyError:
      discard runUp(project, fb.port(), pastStartPeriodClock)

    check not fb.calls.anyIt(it.kind == ckCreateContainer and it.arg == "api")
    check not fb.calls.anyIt(it.kind == ckStartContainer and it.arg == "api")
    check fb.calls[^2 .. ^1] == @[
      Call(kind: ckStopContainer, arg: "db"),
      Call(kind: ckRemoveContainer, arg: "db"),
    ]

  test "backend failure: startContainer failing cleans up the container whose create already succeeded":
    let project = ComposeProject(services: @[svc("db", healthcheck = true)])
    let fb = newFakeBackend()
    fb.failOn(ckStartContainer, "db")

    expect RuntimeError:
      discard runUp(project, fb.port(), pastStartPeriodClock)

    check fb.calls == @[
      Call(kind: ckResolveImage, arg: "registry.example.com/db:1"),
      Call(kind: ckCreateContainer, arg: "db"),
      Call(kind: ckStartContainer, arg: "db"),
      Call(kind: ckStopContainer, arg: "db"),
      Call(kind: ckRemoveContainer, arg: "db"),
    ]

  test "journal: cleanup is driven by what was actually created, not the full plan, and never doubled":
    let project = ComposeProject(services: @[
      svc("api", dependsOn = @["db"]),
      svc("db", healthcheck = true)
    ])
    let fb = newFakeBackend()
    fb.scriptProbes("db", @[0]) # db reaches healthy
    fb.failOn(ckCreateContainer, "api")

    expect RuntimeError:
      discard runUp(project, fb.port(), pastStartPeriodClock)

    # api's create was attempted (and failed) but never journaled, so
    # cleanup must touch db exactly once and must never touch api.
    check fb.calls.filterIt(it.kind == ckStopContainer) == @[Call(kind: ckStopContainer, arg: "db")]
    check fb.calls.filterIt(it.kind == ckRemoveContainer) == @[Call(kind: ckRemoveContainer, arg: "db")]
    check not fb.calls.anyIt(it.arg == "api" and it.kind in {ckStopContainer, ckRemoveContainer})

suite "newRealProbeClock":
  test "attempt 0 fires immediately (no sleep), later attempts actually wait `interval`":
    let clock = newRealProbeClock()
    let interval = initDuration(milliseconds = 50)
    let before = getTime()
    let elapsed0 = clock(0, interval)
    check elapsed0 < initDuration(milliseconds = 20)
    let elapsed1 = clock(1, interval)
    let wallElapsed = getTime() - before
    check elapsed1 >= interval
    check wallElapsed >= interval
