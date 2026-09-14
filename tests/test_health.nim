## Fixture-driven unit tests for the pure health state machine
## (src/navisoma/health.nim), per #18 phase 1's required fixtures:
## healthy, start-period recovery, and retries-exhausted.

import std/[unittest, times]
import navisoma/types
import navisoma/health

let spec = HealthCheckSpec(
  test: @["CMD", "pg_isready", "-U", "app"],
  interval: initDuration(seconds = 5),
  timeout: initDuration(seconds = 3),
  retries: 3,
  startPeriod: initDuration(seconds = 10))

suite "health state machine":
  test "a single successful probe reaches healthy":
    let s0 = initHealthState()
    let s1 = stepHealth(spec, s0, probeSuccess, initDuration(seconds = 2))
    check s1.phase == hpHealthy
    check s1.consecutiveFailures == 0

  test "a failure during start_period does not count toward retries, and a later success recovers to healthy":
    let s0 = initHealthState()
    check s0.phase == hpStarting

    # Two failures, both inside the 10s start_period: still starting, and
    # the failure count is not incremented (start_period exempts them).
    let s1 = stepHealth(spec, s0, probeFailure, initDuration(seconds = 2))
    check s1.phase == hpStarting
    check s1.consecutiveFailures == 0

    let s2 = stepHealth(spec, s1, probeFailure, initDuration(seconds = 8))
    check s2.phase == hpStarting
    check s2.consecutiveFailures == 0

    # A success after start_period has elapsed still recovers immediately.
    let s3 = stepHealth(spec, s2, probeSuccess, initDuration(seconds = 12))
    check s3.phase == hpHealthy
    check s3.consecutiveFailures == 0

  test "retries consecutive failures after start_period elapses reaches unhealthy":
    let s0 = initHealthState()
    # start_period has already elapsed for every probe below.
    let s1 = stepHealth(spec, s0, probeFailure, initDuration(seconds = 15))
    check s1.phase == hpStarting # not yet at `retries` failures
    check s1.consecutiveFailures == 1

    let s2 = stepHealth(spec, s1, probeFailure, initDuration(seconds = 20))
    check s2.phase == hpStarting
    check s2.consecutiveFailures == 2

    let s3 = stepHealth(spec, s2, probeFailure, initDuration(seconds = 25))
    check s3.phase == hpUnhealthy # retries == 3 reached
    check s3.consecutiveFailures == 3

  test "a service that was healthy can become unhealthy from later failures":
    let healthy = HealthState(phase: hpHealthy, consecutiveFailures: 0)
    let f1 = stepHealth(spec, healthy, probeFailure, initDuration(seconds = 100))
    let f2 = stepHealth(spec, f1, probeFailure, initDuration(seconds = 105))
    let f3 = stepHealth(spec, f2, probeFailure, initDuration(seconds = 110))
    check f1.phase == hpHealthy
    check f2.phase == hpHealthy
    check f3.phase == hpUnhealthy
