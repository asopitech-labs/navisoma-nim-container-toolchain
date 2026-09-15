## Fixture-driven unit tests for the deterministic planner
## (src/navisoma/planner.nim), per #18 phase 1's required
## cycle/unknown-service rejection fixture, plus the deterministic-order
## property the acceptance criteria depend on.

import std/[unittest, options, times]
import navisoma/types
import navisoma/errors
import navisoma/planner

proc svc(name: string, healthcheck = false, dependsOn: seq[string] = @[]): ServiceSpec =
  result = ServiceSpec(name: name, image: "registry.example.com/" & name & ":1")
  if healthcheck:
    result.healthcheck = some(HealthCheckSpec(retries: 3, interval: initDuration(seconds = 5),
                                                timeout: initDuration(seconds = 3), startPeriod: initDuration()))
  for dep in dependsOn:
    result.dependsOn.add DependsOnEdge(service: dep, condition: conditionServiceHealthy)

suite "planner":
  test "depends_on naming an undeclared service is rejected":
    let project = ComposeProject(services: @[
      svc("api", dependsOn = @["db"])
    ])
    expect ComposeSemanticError:
      discard planUp(project)

  test "a depends_on cycle is rejected":
    # Both sides need a healthcheck so the cycle check, not the
    # missing-healthcheck check, is what's under test here.
    let project = ComposeProject(services: @[
      svc("a", healthcheck = true, dependsOn = @["b"]),
      svc("b", healthcheck = true, dependsOn = @["a"])
    ])
    expect PlanningError:
      discard planUp(project)

  test "a service depending on itself is rejected as a cycle":
    let project = ComposeProject(services: @[
      svc("a", healthcheck = true, dependsOn = @["a"])
    ])
    expect PlanningError:
      discard planUp(project)

  test "depends_on service_healthy targeting a service without a healthcheck is rejected, and no actions are generated":
    let project = ComposeProject(services: @[
      svc("api", dependsOn = @["db"]),
      svc("db") # no healthcheck
    ])
    expect ComposeSemanticError:
      discard planUp(project)
    var producedAnyAction = false
    try:
      discard planUp(project)
      producedAnyAction = true
    except ComposeSemanticError:
      discard
    check not producedAnyAction

  test "a healthy dependency is planned, started, and awaited before its dependent, deterministically":
    let project = ComposeProject(services: @[
      svc("api", dependsOn = @["db"]),
      svc("db", healthcheck = true)
    ])
    let trace = planUp(project)
    check trace == @[
      Action(kind: akResolveImage, service: "db"),
      Action(kind: akCreateContainer, service: "db"),
      Action(kind: akStartContainer, service: "db"),
      Action(kind: akAwaitHealth, service: "db"),
      Action(kind: akResolveImage, service: "api"),
      Action(kind: akCreateContainer, service: "api"),
      Action(kind: akStartContainer, service: "api"),
    ]
    # Re-planning the same fixture produces the exact same trace.
    check planUp(project) == trace

  test "planDown reverses exactly the services planUp created":
    let project = ComposeProject(services: @[
      svc("api", dependsOn = @["db"]),
      svc("db", healthcheck = true)
    ])
    let downTrace = planDown(planUp(project))
    check downTrace == @[
      Action(kind: akStopContainer, service: "api"),
      Action(kind: akRemoveContainer, service: "api"),
      Action(kind: akStopContainer, service: "db"),
      Action(kind: akRemoveContainer, service: "db"),
    ]
