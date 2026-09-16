## The backend port (#18 phase 2): the smallest runtime surface the
## executor needs — image resolve, create/start/stop/remove, and
## health-command exec — per docs/architecture.md section 4
## ("Runtime-neutral semantics").
##
## Every operation is addressed by a NAVISOMA-level identifier (an image
## reference, a service name); no backend-native handle or type appears
## in this port. A concrete adapter (the fake backend under tests/, and
## later the containerd/WSLC adapters) keeps whatever native handle it
## needs to remember between calls inside its own closures/state — that
## state's type never appears here, so it can never leak into the
## executor, planner, or CLI.

import std/times
import ./types

type
  ResolvedImage* = object
    ## A backend-opaque but NAVISOMA-level image identifier (e.g. the
    ## reference or digest an adapter's backend already exposes
    ## publicly) — never an internal native handle.
    id*: string

  ProbeResult* = object
    exitCode*: int

  BackendPort* = object
    resolveImage*: proc (image: string): ResolvedImage {.closure.}
    createContainer*: proc (service: ServiceSpec, image: ResolvedImage) {.closure.}
    startContainer*: proc (service: string) {.closure.}
    stopContainer*: proc (service: string) {.closure.}
    removeContainer*: proc (service: string) {.closure.}
    execHealthProbe*: proc (service: string, test: seq[string], timeout: Duration): ProbeResult {.closure.}
      ## `timeout` is the healthcheck's per-probe deadline (Compose
      ## `healthcheck.timeout`) — an adapter that can hang (a real backend
      ## waiting on the probed process to exit) must enforce it and report a
      ## timeout as a nonzero `ProbeResult.exitCode`, never let the call
      ## block past it.
