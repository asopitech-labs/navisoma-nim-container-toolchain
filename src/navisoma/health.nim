## Pure health-state derivation: "N periodic exec exit codes, interpreted
## against interval/retries/start_period" -> a named health state. This is
## the backend-neutral "integration responsibility" #16 found reducible to
## generic exec/lifecycle primitives (see
## docs/validation/irreducible-core-counterfactual.md) — it is intentionally
## planner-owned and independent of any backend type.
##
## No I/O, no clock, no backend call: every transition is a pure function of
## the previous state, one probe outcome, and how much time has elapsed
## since the container was judged startable. The executor (a later phase)
## owns actually running the probe command and measuring elapsed time.

import std/times
import ./types

type
  HealthPhase* = enum
    hpStarting, hpHealthy, hpUnhealthy

  ProbeOutcome* = enum
    probeSuccess, probeFailure

  HealthState* = object
    phase*: HealthPhase
    consecutiveFailures*: int

proc initHealthState*(): HealthState =
  HealthState(phase: hpStarting, consecutiveFailures: 0)

proc stepHealth*(spec: HealthCheckSpec, state: HealthState,
                  outcome: ProbeOutcome, elapsedSinceStart: Duration): HealthState =
  ## One probe result in, one new state out.
  ##
  ## Compose Specification `healthcheck.start_period` semantics: failures
  ## occurring before `start_period` has elapsed do not count toward
  ## `retries`, but a success at any time immediately marks the service
  ## healthy. Once the service has been healthy at least once, the start
  ## period no longer exempts new failures — a service that later starts
  ## failing again must count those failures normally, since "still
  ## starting" is no longer true of it.
  case outcome
  of probeSuccess:
    HealthState(phase: hpHealthy, consecutiveFailures: 0)
  of probeFailure:
    let exemptedByStartPeriod = state.phase == hpStarting and elapsedSinceStart < spec.startPeriod
    if exemptedByStartPeriod:
      HealthState(phase: hpStarting, consecutiveFailures: state.consecutiveFailures)
    else:
      let failures = state.consecutiveFailures + 1
      if failures >= spec.retries:
        HealthState(phase: hpUnhealthy, consecutiveFailures: failures)
      else:
        HealthState(phase: state.phase, consecutiveFailures: failures)
