# NAVISOMA

NAVISOMA is a small Nim CLI that runs a health-gated Compose workflow, plus a
one-shot migration gate, on containerd or WSL Containers (WSLC). It owns the
restricted Compose parsing, execution order, and reverse cleanup; the selected
backend owns runtime calls and native handles.

## MVP status

The constrained MVP accepts health gates and one migration-completion gate.
The migration workflow passed a live containerd scenario and a Docker Engine
v27.5.1 / Compose v2.33.0 differential run on 2026-10-06. The WSLC binary
cross-compiles against SDK 2.9.9 and has the same fixture; its new migration
scenario still needs a recorded Windows live run. See
[MVP support and cleanup](docs/mvp-support.md) for the precise evidence.
The dedicated WSLC workflow intentionally reports an unexecuted failure until
its labeled Windows runner is online; its setup is documented in
[the WSLC runbook](docs/wslc-dev-environment.md).

## Operations

```text
navisoma plan --backend <containerd|wslc> compose.yaml
navisoma up   --backend <containerd|wslc> compose.yaml
navisoma down --backend <containerd|wslc> compose.yaml
```

`plan` prints the backend-neutral action trace. `up` resolves/pulls images,
creates and starts services in dependency order, waits for health gates or a
required migration exit code of zero, then starts dependents. `down` stops and
removes only resources created by that invocation, in reverse order.

## Supported Compose subset

- `services`, `image`, `command`, `environment`;
- `healthcheck.test`, `interval`, `timeout`, `retries`, `start_period`;
- `depends_on.<service>.condition: service_healthy`;
- `depends_on.<service>.condition: service_completed_successfully`;
- prebuilt images only.

`build`, volumes, networks, ports, secrets/configs, profiles, includes,
extends, merge, multi-project reconciliation, macOS, GPU, and broad Compose
conformance are intentionally unsupported.

## Build and verification

```bash
nimble test
tests/integration/containerd/run.sh
tests/integration/wslc/run.sh
tests/differential/docker-compose/run.sh  # requires official Docker Compose v2+
```

The native integration scripts provision their toolchain in containers and
clean their test resources. See [MVP support and cleanup](docs/mvp-support.md)
for backend prerequisites, ownership, and the exact oracle contract.

GitHub Actions runs the unit/CLI test, Compose-script syntax check, and
whitespace check for every push to `main` and pull request. It uses Nim 2.2.10
and Nimble 0.22.2. Reproduce the CI checks locally with:

```bash
nimble test -Y
bash -n tests/differential/docker-compose/run.sh
git diff --check
```

The native integration scripts above remain separate local verification.

## Further reading

- [containerd implementation record](docs/containerd-dev-environment.md)
- [WSLC build and live-host record](docs/wslc-dev-environment.md)
- [next user-value decision](docs/validation/next-user-value-decision.md)
- [architecture](docs/architecture.md)
- [research and longer-term plans](docs/implementation-plan.md)

## License

See [LICENSE](LICENSE).
