## containerd BackendPort adapter (#18 phase 3): builds a BackendPort (../backend.nim) whose
## closures call into the containerd client (src/bindings/containerd_client.nim). This is the
## only file that holds a ContainerdClient/native handle — nothing above it (planner, executor,
## CLI) ever sees containerd-specific state, per backend.nim's own boundary ("A concrete adapter
## ... keeps whatever native handle it needs to remember between calls inside its own
## closures/state — that state's type never appears here").

when not defined(navisomaContainerd):
  {.error: "src/navisoma/backends/containerd_backend.nim must only be imported under -d:navisomaContainerd".}

import ../backend
import ../types
import ../../bindings/containerd_client
# Callers of newContainerdPort() need `close` in scope for the returned client — Nim does not
# transitively expose an imported module's symbols without an explicit re-export.
export containerd_client.ContainerdClient, containerd_client.close

const
  DefaultSocketPath* = "/run/containerd/containerd.sock"
  DefaultNamespace* = "navisoma"
    ## A NAVISOMA-owned namespace, not containerd's "default" — keeps NAVISOMA-managed
    ## containers/images/snapshots separate from anything else using the same daemon. No
    ## explicit namespace-creation call is needed: containerd namespaces are created implicitly
    ## on first write (verified against the pinned daemon during development).

proc newContainerdPort*(socketPath: string = DefaultSocketPath,
                         containerdNamespace: string = DefaultNamespace
                        ): tuple[port: BackendPort, client: ContainerdClient] =
  let client = connect(socketPath, containerdNamespace)

  var port: BackendPort
  port.resolveImage = proc (image: string): ResolvedImage =
    ResolvedImage(id: client.resolveImage(image))

  port.createContainer = proc (service: ServiceSpec, image: ResolvedImage) =
    client.createContainer(service.name, image.id, service.command, service.environment)

  port.startContainer = proc (service: string) =
    client.startContainer(service)

  port.stopContainer = proc (service: string) =
    client.stopContainer(service)

  port.removeContainer = proc (service: string) =
    client.removeContainer(service)

  port.execHealthProbe = proc (service: string, test: seq[string]): ProbeResult =
    ProbeResult(exitCode: client.execHealthProbe(service, test))

  (port, client)
