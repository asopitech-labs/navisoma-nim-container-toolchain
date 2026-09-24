# Package

version       = "0.1.0"
author        = "asopitech"
description   = "NAVISOMA: health-gated Compose subset across containerd and WSLC backends"
license       = "MIT"
srcDir        = "src"
bin           = @["navisoma"]

# Dependencies

requires "nim >= 2.0.0"
requires "yaml >= 2.2.1"

task test, "Run the Phase 1 pure-semantics and Phase 2 executor test suite":
  exec "nim c --path:src -r tests/test_health.nim"
  exec "nim c --path:src -r tests/test_planner.nim"
  exec "nim c --path:src -r tests/test_compose_parser.nim"
  exec "nim c --path:src -r tests/test_executor.nim"
  exec "nim c --path:src -r tests/test_wslc_names.nim"
