## Canonical application model for the #18 MVP boundary only: `services`,
## `image`, `command`, `environment`, `healthcheck.*`, and
## `depends_on.<service>.condition: service_healthy`. No Docker, containerd,
## WSLC, gRPC, or protobuf type appears here — see docs/architecture.md
## section 2.

import std/[options, times]

type
  HealthCheckSpec* = object
    test*: seq[string]
    interval*: Duration
    timeout*: Duration
    retries*: int
    startPeriod*: Duration

  DependsOnCondition* = enum
    ## The MVP boundary supports exactly one condition; other Compose
    ## conditions (`service_started`, `service_completed_successfully`) are
    ## explicitly deferred per #18's fixed MVP boundary.
    conditionServiceHealthy

  DependsOnEdge* = object
    service*: string
    condition*: DependsOnCondition

  ServiceSpec* = object
    name*: string
    image*: string
    command*: seq[string]
    environment*: seq[tuple[key, value: string]]
    healthcheck*: Option[HealthCheckSpec]
    dependsOn*: seq[DependsOnEdge]

  ComposeProject* = object
    ## `services` preserves source document order: this is the tie-break
    ## the planner uses to make its action trace deterministic (#18
    ## acceptance criteria: the same fixture must produce the same trace).
    services*: seq[ServiceSpec]

proc findService*(project: ComposeProject, name: string): Option[ServiceSpec] =
  for svc in project.services:
    if svc.name == name:
      return some(svc)
  none(ServiceSpec)

proc hasService*(project: ComposeProject, name: string): bool =
  project.findService(name).isSome
