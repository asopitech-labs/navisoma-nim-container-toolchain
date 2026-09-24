## Stable names shared by the WSLC client process and its session-owning
## daemon. WSLC cannot re-attach a session from another process, so both
## commands must derive the same local pipe name from the Compose file id.

proc wslcContainerId*(projectId, service: string): string =
  "nvsm-" & projectId & "-" & service

proc wslcPipeName*(projectId: string): string =
  r"\\.\pipe\navisoma-wslc-" & projectId
