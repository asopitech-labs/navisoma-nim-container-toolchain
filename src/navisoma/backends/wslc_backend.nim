## WSLC BackendPort adapter. It is owned by the daemon process because the
## WSLC SDK has no cross-process session re-attach.

when not defined(navisomaWslc):
  {.error: "src/navisoma/backends/wslc_backend.nim must only be imported under -d:navisomaWslc".}

import std/times
import ../backend
import ../types
import ../../bindings/wslc_client
import ../wslc_names

export wslc_client.WslcClient, wslc_client.close

proc newWslcPort*(projectId: string): tuple[port: BackendPort, client: WslcClient] =
  let client = newWslcClient(projectId)

  var port: BackendPort
  port.resolveImage = proc(image: string): ResolvedImage =
    ResolvedImage(id: client.resolveImage(image))
  port.createContainer = proc(service: ServiceSpec, image: ResolvedImage) =
    client.createContainer(wslcContainerId(projectId, service.name), image.id, service.command, service.environment)
  port.startContainer = proc(service: string) =
    client.startContainer(wslcContainerId(projectId, service))
  port.stopContainer = proc(service: string) =
    client.stopContainer(wslcContainerId(projectId, service))
  port.removeContainer = proc(service: string) =
    client.removeContainer(wslcContainerId(projectId, service))
  port.execHealthProbe = proc(service: string, test: seq[string], timeout: Duration): ProbeResult =
    ProbeResult(exitCode: client.execHealthProbe(wslcContainerId(projectId, service), test, timeout.inMilliseconds.int64))

  (port, client)
