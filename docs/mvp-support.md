# NAVISOMA MVP support and cleanup

## Purpose and current evidence

This document is the user-facing boundary for Issue #18's fixed MVP. The same
health-gated dependency workflow is verified against containerd and WSLC. The
Docker Compose oracle scenario is committed but needs an official Docker
Compose v2 host to produce its final evidence; Podman Compose is deliberately
not accepted as that oracle.

## Supported behavior

| Area | MVP contract |
| --- | --- |
| Input | `services`, `image`, `command`, `environment`, the five healthcheck fields, and `depends_on.<service>.condition: service_healthy` |
| Planning | `plan --backend containerd` and `plan --backend wslc` produce the same action trace |
| Startup | A dependent starts only after its dependency is healthy |
| Failure | An unhealthy dependency prevents its dependent from starting and returns `service '<name>' is unhealthy after <retries> retries` |
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

`tests/differential/docker-compose/run.sh` uses the same healthy and unhealthy
fixtures as the containerd integration. It requires `docker compose version`
to identify itself as Docker Compose v2, then proves:

1. `api` starts after `db`'s first successful healthcheck.
2. An unhealthy `db` makes `up --wait` fail and never leaves `api` running.
3. Both temporary Compose projects are removed by the script's exit trap.

The script exits 77 when Docker Compose v2 is unavailable. This is a skip, not
a passing oracle result; do not substitute Podman Compose for this check.

## Verification order

```bash
nimble test
tests/integration/containerd/run.sh
tests/integration/wslc/run.sh
tests/differential/docker-compose/run.sh
```

The first three have current evidence. Run the final command on an official
Docker Compose v2 host before closing Issue #18.
