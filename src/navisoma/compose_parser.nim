## Restricted Compose parser for #18's fixed MVP boundary only.
##
## Supported input: `services`, `image`, `command`, `environment`,
## `healthcheck.{test,interval,timeout,retries,start_period}`,
## `depends_on.<service>.condition: service_healthy`, prebuilt images only.
## Every other Compose key — `build:`, `networks`, `volumes`, `ports`,
## `secrets`/`configs`, `profiles`, `include`/`extends`/merge, and any
## `depends_on` condition other than `service_healthy` — is explicitly
## deferred (per #18) and rejected here, before any planning happens, per
## #18's semantic failure contract ("An unsupported input ... fails before
## partial execution").
##
## Uses NimYAML's DOM API for YAML syntax (generic library reuse, per
## docs/architecture.md section 1); the allow-listing and shape checks
## below are the Compose-specific, NAVISOMA-owned part.

import std/[options, strutils, times, tables]
import yaml/dom, yaml/loading
import ./types
import ./errors

const
  SupportedServiceKeys = ["image", "command", "environment", "healthcheck", "depends_on"]
  SupportedHealthcheckKeys = ["test", "interval", "timeout", "retries", "start_period"]
  SupportedDependsOnEdgeKeys = ["condition"]

proc fail(msg: string) =
  raise newException(SchemaError, msg)

proc requireMapping(node: YamlNode, context: string): YamlNode =
  if node.kind != yMapping:
    fail(context & " must be a mapping")
  node

proc requireSequence(node: YamlNode, context: string): YamlNode =
  if node.kind != ySequence:
    fail(context & " must be a sequence")
  node

proc requireScalar(node: YamlNode, context: string): string =
  if node.kind != yScalar:
    fail(context & " must be a scalar")
  node.content

proc hasKey(node: YamlNode, key: string): bool =
  try:
    discard node[key]
    true
  except KeyError:
    false

proc checkAllowedKeys(node: YamlNode, allowed: openArray[string], context: string) =
  for key in node.fields.keys:
    let name = requireScalar(key, context & " key")
    if name notin allowed:
      fail(context & ": unsupported key '" & name & "' (this MVP boundary does not support it)")

proc parseDuration(raw: string, context: string): Duration =
  ## Compose-style short duration strings: an integer followed by a single
  ## unit (`s`, `ms`, `m`, `h`). This MVP boundary does not need combined
  ## forms like `1h30m`.
  if raw.len < 2:
    fail(context & ": '" & raw & "' is not a valid duration")
  var unitStart = -1
  for i, c in raw:
    if c notin {'0'..'9', '.'}:
      unitStart = i
      break
  if unitStart <= 0:
    fail(context & ": '" & raw & "' is not a valid duration")
  let numPart = raw[0 ..< unitStart]
  let unit = raw[unitStart .. ^1]
  var amount: float
  try:
    amount = parseFloat(numPart)
  except ValueError:
    fail(context & ": '" & raw & "' is not a valid duration")
  case unit
  of "ms": initDuration(milliseconds = amount.int)
  of "s": initDuration(milliseconds = (amount * 1000).int)
  of "m": initDuration(milliseconds = (amount * 60_000).int)
  of "h": initDuration(milliseconds = (amount * 3_600_000).int)
  else:
    fail(context & ": unsupported duration unit in '" & raw & "'")
    initDuration()

proc parseHealthcheck(node: YamlNode, context: string): HealthCheckSpec =
  discard requireMapping(node, context)
  checkAllowedKeys(node, SupportedHealthcheckKeys, context)
  result = HealthCheckSpec(retries: 3, interval: initDuration(seconds = 30),
                            timeout: initDuration(seconds = 30), startPeriod: initDuration())
  if not node.hasKey("test"):
    fail(context & ".test is required and must be a non-empty sequence")
  let testNode = node["test"]
  discard requireSequence(testNode, context & ".test")
  if testNode.len == 0:
    fail(context & ".test must not be empty")
  result.test = @[]
  for elem in testNode.elems:
    result.test.add requireScalar(elem, context & ".test element")
  if node.hasKey("interval"):
    result.interval = parseDuration(requireScalar(node["interval"], context & ".interval"), context & ".interval")
  if node.hasKey("timeout"):
    result.timeout = parseDuration(requireScalar(node["timeout"], context & ".timeout"), context & ".timeout")
  if node.hasKey("start_period"):
    result.startPeriod = parseDuration(requireScalar(node["start_period"], context & ".start_period"), context & ".start_period")
  if node.hasKey("retries"):
    let raw = requireScalar(node["retries"], context & ".retries")
    try:
      result.retries = parseInt(raw)
    except ValueError:
      fail(context & ".retries: '" & raw & "' is not an integer")
  if result.retries <= 0:
    fail(context & ".retries must be a positive integer")
  if result.interval.inMilliseconds <= 0:
    fail(context & ".interval must be a positive duration")
  if result.timeout.inMilliseconds <= 0:
    fail(context & ".timeout must be a positive duration")

proc parseDependsOn(node: YamlNode, context: string): seq[DependsOnEdge] =
  discard requireMapping(node, context)
  for key, value in node.fields.pairs:
    let serviceName = requireScalar(key, context & " key")
    discard requireMapping(value, context & "." & serviceName)
    checkAllowedKeys(value, SupportedDependsOnEdgeKeys, context & "." & serviceName)
    if not value.hasKey("condition"):
      fail(context & "." & serviceName & ": missing required 'condition'")
    let condition = requireScalar(value["condition"], context & "." & serviceName & ".condition")
    if condition != "service_healthy":
      fail(context & "." & serviceName & ".condition: unsupported condition '" & condition &
        "' (this MVP boundary supports only 'service_healthy')")
    result.add DependsOnEdge(service: serviceName, condition: conditionServiceHealthy)

proc parseEnvironment(node: YamlNode, context: string): seq[tuple[key, value: string]] =
  case node.kind
  of yMapping:
    for key, value in node.fields.pairs:
      result.add (requireScalar(key, context & " key"), requireScalar(value, context & " value"))
  of ySequence:
    for elem in node.elems:
      let raw = requireScalar(elem, context & " element")
      let parts = raw.split('=', maxsplit = 1)
      if parts.len != 2:
        fail(context & ": '" & raw & "' is not in KEY=VALUE form")
      result.add (parts[0], parts[1])
  of yScalar:
    fail(context & " must be a mapping or a sequence")

proc parseService(name: string, node: YamlNode): ServiceSpec =
  let context = "services." & name
  discard requireMapping(node, context)
  checkAllowedKeys(node, SupportedServiceKeys, context)
  if not node.hasKey("image"):
    fail(context & ": 'image' is required (this MVP boundary supports prebuilt images only)")
  result = ServiceSpec(name: name, image: requireScalar(node["image"], context & ".image"))
  if node.hasKey("command"):
    for elem in requireSequence(node["command"], context & ".command").elems:
      result.command.add requireScalar(elem, context & ".command element")
  if node.hasKey("environment"):
    result.environment = parseEnvironment(node["environment"], context & ".environment")
  if node.hasKey("healthcheck"):
    result.healthcheck = some(parseHealthcheck(node["healthcheck"], context & ".healthcheck"))
  if node.hasKey("depends_on"):
    result.dependsOn = parseDependsOn(node["depends_on"], context & ".depends_on")

proc parseComposeProject*(source: string): ComposeProject =
  var root: YamlNode
  try:
    load(source, root)
  except YamlLoadingError as e:
    fail("invalid YAML: " & e.msg)
  discard requireMapping(root, "document root")
  checkAllowedKeys(root, ["services"], "document root")
  if not root.hasKey("services"):
    fail("document root: 'services' is required")
  let servicesNode = requireMapping(root["services"], "services")
  for key, value in servicesNode.fields.pairs:
    let name = requireScalar(key, "services key")
    result.services.add parseService(name, value)
