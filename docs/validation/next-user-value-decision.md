# Migration-gate implementation record — Issue #23

## Decision summary

The adopted promise is implemented: a platform engineer can use one restricted Compose
file to run a database migration after a dependency is healthy and before an
API starts, on either containerd or WSLC. The implementation is limited to
[`service_completed_successfully`](https://github.com/compose-spec/compose-spec/blob/main/05-services.md)
and is tracked by [Issue #23](https://github.com/asopitech-labs/navisoma-nim-container-toolchain/issues/23).

This is a narrow product bet, not evidence of a general NAVISOMA semantic
core. The fixed MVP boundary remains in force except for this one condition.

## Target user and workflow

The target user operates the same small, database-backed service on Linux
containerd and Windows WSLC. Their Compose file has three services:

```yaml
services:
  db:      # becomes healthy
  migrate: # applies schema and exits
  api:     # may start only after migrate exits 0
```

They run `navisoma plan` to review the stable action order, then `navisoma
up` on either selected backend. `navisoma down` remains responsible only for
resources created by that invocation.

## Observable contract

On either backend, the order is `db healthy` → `migrate exit 0` → `api
create/start`. If `migrate` exits nonzero, NAVISOMA reports:

```text
service '<name>' did not complete successfully (exit code <n>)
```

It must not create or start `api`, and it must reverse-clean only the
invocation-owned `migrate` and dependency resources. Plans and diagnostics
must not expose backend-native handles or API types.

## Why the selected alternatives do not meet this contract

| Alternative | What it provides | Missing part |
| --- | --- | --- |
| [Docker Compose](https://docs.docker.com/compose/how-tos/startup-order/) | Documents `service_completed_successfully` and health-gated ordering. | It targets Docker Engine, not the selected containerd and WSLC APIs. |
| [nerdctl Compose](https://github.com/containerd/nerdctl/blob/main/docs/compose.md) | A Compose-compatible CLI for containerd. | It has no WSLC backend, so it cannot provide the same two-backend contract. |
| [WSL container API](https://learn.microsoft.com/en-us/windows/wsl/wsl-container) | Session, image, container, and process lifecycle primitives. | Its documented API has no Compose project reader or dependency scheduler; the user would need separate orchestration. |

The difference is therefore not a broader Compose implementation. It is one
source file, action order, semantic failure, and cleanup contract across the
two selected native runtimes without Docker Engine or a second orchestration
script.

## Implementation and evidence

Issue #23 adds only:

- parser acceptance for `service_completed_successfully`;
- a planned wait-for-completion action and one backend-port operation that
  returns the init process exit code;
- the success/failure behavior above in the executor and adapters;
- parser/planner, fake-backend, containerd, WSLC, and Docker Compose
  differential evidence for the three-service workflow.

It does not add `build`, volumes, networks, ports, restart behavior,
profiles, include/extends, reconciliation, or generalized job management.

On 2026-10-06, `nimble test` and the disposable containerd scenario passed:
the successful fixture left `db` and `api` running after `migrate` stopped
with exit 0, and `down` removed all three. The failed fixture reported the
stable semantic error, created no API, and removed the migration and database
in reverse order. Docker Engine v27.5.1 with Compose v2.33.0 showed the same
start gate in a disposable daemon. Compose may create its dependent container
before a condition resolves, so that oracle asserts no API start; NAVISOMA's
stricter no-create rule is verified by its fake and containerd scenarios.

The WSLC adapter now waits for the documented init-process exit status, its
Windows binary cross-compiles against SDK 2.9.9, and the same success/failure
fixtures are ready in `tests/integration/wslc/`. A live Windows invocation of
those new fixtures remains the outstanding evidence item.

## Decision record

- Current MVP evidence: [MVP support and cleanup](../mvp-support.md)
- Prior product gate: [project continuation decision](project-continuation-decision.md)
- Prior alternative analysis: [irreducible-core counterfactual](irreducible-core-counterfactual.md)
- Implementation issue: [#23](https://github.com/asopitech-labs/navisoma-nim-container-toolchain/issues/23)
