# Next user-value decision — Issue #21

## Decision summary

Adopt one next promise: a platform engineer can use one restricted Compose
file to run a database migration after a dependency is healthy and before an
API starts, on either containerd or WSLC. The implementation is limited to
[`service_completed_successfully`](https://github.com/compose-spec/compose-spec/blob/main/05-services.md)
and is tracked by [Issue #23](https://github.com/asopitech-labs/navisoma-nim-container-toolchain/issues/23).

The current MVP intentionally rejects that condition before planning; it is
the specific gap that Issue #23 may close.

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

## Minimum implementation and evidence

Issue #23 may add only:

- parser acceptance for `service_completed_successfully`;
- a planned wait-for-completion action and one backend-port operation that
  returns the init process exit code;
- the success/failure behavior above in the executor and adapters;
- parser/planner, fake-backend, containerd, WSLC, and Docker Compose
  differential evidence for the three-service workflow.

It must not add `build`, volumes, networks, ports, restart behavior,
profiles, include/extends, reconciliation, or generalized job management.

## Decision record

- Current MVP evidence: [MVP support and cleanup](../mvp-support.md)
- Prior product gate: [project continuation decision](project-continuation-decision.md)
- Prior alternative analysis: [irreducible-core counterfactual](irreducible-core-counterfactual.md)
- Child implementation issue: [#23](https://github.com/asopitech-labs/navisoma-nim-container-toolchain/issues/23)
