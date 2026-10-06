# NAVISOMA MVP support and cleanup

## Purpose and current evidence

This document is the user-facing boundary for the constrained MVP. It supports
health-gated dependencies and one-shot migrations gated by
`service_completed_successfully`.

The migration workflow has the following current evidence:

- `nimble test` covers parser, planner, CLI trace, and fake backend success/
  failure cleanup;
- `tests/integration/containerd/run.sh` passed on 2026-10-06 against the
  pinned containerd v2.3.5 daemon;
- Docker Compose v2.33.0 against Docker Engine v27.5.1 passed in disposable
  Linux Docker-in-Docker on 2026-10-06;
- the WSLC binary cross-compiles against SDK 2.9.9 and the Windows fixture is
  present, but this host cannot run its new migration scenario live.

The Docker Engine run exited 0 with `OK: Docker Engine Compose migration-gate
oracle` and removed both temporary projects. It is oracle evidence for Compose
orchestration, not general Docker Engine conformance.

## Supported behavior

| Area | MVP contract |
| --- | --- |
| Input | `services`, `image`, `command`, `environment`, the five healthcheck fields, and `depends_on.<service>.condition: service_healthy` or `service_completed_successfully` |
| Planning | `plan --backend containerd` and `plan --backend wslc` produce the same action trace |
| Startup | A dependent starts only after its dependency is healthy, or after a one-shot migration exits 0 |
| Failure | An unhealthy dependency prevents its dependent from starting; a failed migration returns `service '<name>' did not complete successfully (exit code <n>)` and NAVISOMA does not create/start its dependent |
| Teardown | `down` stops/removes only invocation-owned services in reverse order |

Everything else is rejected before runtime work: image builds, volumes,
networks, ports, secrets/configs, profiles, includes, extends, merges,
multi-project reconciliation, GPU, macOS, and general Compose compatibility.

## Backend requirements

| Backend | Native input and host | Build and scenario |
| --- | --- | --- |
| containerd | Pinned containerd v2.3.5, generated gRPC/protobuf facade, and a disposable daemon in Podman or Docker | `tests/integration/containerd/run.sh` |
| WSLC | `Microsoft.WSL.Containers` 2.9.9 fetched and hash-checked during the build; Windows WSL service on the recorded Windows + WSL host | `tests/integration/wslc/run.sh` |

The containerd and WSLC native details, pinned inputs, and regeneration/build
procedures live in [containerd-dev-environment.md](containerd-dev-environment.md)
and [wslc-dev-environment.md](wslc-dev-environment.md). Neither backend asks a
normal `nimble test` run to install a host-native toolchain.

## Cleanup ownership

- The executor owns the action journal and always reverses only services it
  created after a backend or health failure.
- The containerd adapter owns its task/container/snapshot handles; its
  integration test owns a disposable daemon and tears it down on exit.
- The WSLC adapter owns SDK handles and a project-scoped, owner-only daemon.
  `down` performs teardown and releases the SDK session; a forcibly killed
  daemon cannot be re-attached.
- Each integration script owns only its unique fixture/staging resources and
  refuses to reuse a pre-existing staging directory.

## Docker Compose oracle

`tests/differential/docker-compose/run.sh` uses the same health and migration
fixtures as the containerd integration. It requires `docker compose version`
to identify itself as official Docker Compose v2 or newer, then proves:

1. `api` starts after `db`'s first successful healthcheck.
2. a successful migration exits 0 before `api` starts;
3. a failed migration makes `up --wait` fail and never starts `api`;
4. an unhealthy `db` makes `up --wait` fail and never leaves `api` running;
5. all temporary Compose projects are removed by the script's exit trap.

The script exits 77 when official Docker Compose v2+ is unavailable. This is a
skip, not a passing oracle result; do not substitute Podman Compose for this
check. The recorded Docker Engine run used Compose v2.33.0 and Engine v27.5.1
in a disposable Docker-in-Docker daemon. Its successful migration exited 0
before API start; its failed migration and unhealthy project failed `up --wait`
without starting API. Compose can create a dependent container before its
condition resolves, so the oracle deliberately verifies start order rather
than NAVISOMA's stronger no-create rule. The outer daemon and its Podman
network were removed by the test harness.

## Verification order

```bash
nimble test
tests/integration/containerd/run.sh
tests/integration/wslc/run.sh
tests/differential/docker-compose/run.sh
```

The unit/CLI test, containerd scenario, WSLC cross-build, and Docker Engine
run above have current evidence. The WSLC migration fixture requires the
recorded Windows host; it is not a passing substitute to cross-compile it on
Linux. The final command requires an official Docker Compose v2+ client; a
Docker Engine host is additionally required only for engine-level coverage.
