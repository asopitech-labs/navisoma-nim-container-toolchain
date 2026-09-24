import std/unittest
import navisoma/wslc_names

suite "WSLC daemon names":
  test "project-scoped container and pipe names are stable":
    check wslcContainerId("a1b2c3", "db") == "nvsm-a1b2c3-db"
    check wslcPipeName("a1b2c3") == r"\\.\pipe\navisoma-wslc-a1b2c3"
