# NAVISOMA

NAVISOMA is a small Nim CLI that runs one health-gated Compose workflow on
containerd or WSL Containers (WSLC). It owns the restricted Compose parsing,
health-state derivation, execution order, and reverse cleanup; the selected
backend owns runtime calls and native handles.

## MVP status

The fixed MVP is implemented and has real containerd and WSLC integration
evidence. The Docker Compose differential ran with official Docker Compose
v5.5.1 through Podman's Docker-compatible API. This validates Compose-client
orchestration, not Docker Engine conformance.

## Operations

```text
navisoma plan --backend <containerd|wslc> compose.yaml
navisoma up   --backend <containerd|wslc> compose.yaml
navisoma down --backend <containerd|wslc> compose.yaml
```

`plan` prints the backend-neutral action trace. `up` resolves/pulls images,
creates and starts services in dependency order, and runs a health command
until a dependency is healthy. `down` stops and removes only resources created
by that invocation, in reverse order.

## Supported Compose subset

- `services`, `image`, `command`, `environment`;
- `healthcheck.test`, `interval`, `timeout`, `retries`, `start_period`;
- `depends_on.<service>.condition: service_healthy`;
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

## Further reading

- [containerd implementation record](docs/containerd-dev-environment.md)
- [WSLC build and live-host record](docs/wslc-dev-environment.md)
- [architecture](docs/architecture.md)
- [research and longer-term plans](docs/implementation-plan.md)

## License

See [LICENSE](LICENSE).
