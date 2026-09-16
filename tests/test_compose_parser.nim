## Minimal boundary tests for the restricted Compose parser
## (src/navisoma/compose_parser.nim): the fixed MVP example must parse, and
## a key outside the fixed MVP boundary must be rejected before planning
## happens (per #18's semantic failure contract).

import std/[unittest, options]
import navisoma/types
import navisoma/errors
import navisoma/compose_parser

const fixedExample = """
services:
  db:
    image: registry.example.com/db:14
    healthcheck:
      test: ["CMD", "pg_isready", "-U", "app"]
      interval: 5s
      timeout: 3s
      retries: 5
  api:
    image: registry.example.com/api:2.1.0
    depends_on:
      db:
        condition: service_healthy
"""

suite "restricted Compose parser":
  test "parses the fixed MVP example":
    let project = parseComposeProject(fixedExample)
    check project.services.len == 2
    let db = project.findService("db").get()
    check db.image == "registry.example.com/db:14"
    check db.healthcheck.isSome
    check db.healthcheck.get().retries == 5
    check db.healthcheck.get().test == @["pg_isready", "-U", "app"]
    let api = project.findService("api").get()
    check api.dependsOn.len == 1
    check api.dependsOn[0].service == "db"

  test "strips the 'CMD' prefix rather than passing it through as an argument":
    let source = """
services:
  db:
    image: registry.example.com/db:1
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost/health"]
      interval: 5s
      timeout: 3s
      retries: 5
"""
    let db = parseComposeProject(source).findService("db").get()
    check db.healthcheck.get().test == @["curl", "-f", "http://localhost/health"]

  test "rewrites 'CMD-SHELL' into a /bin/sh -c invocation":
    let source = """
services:
  db:
    image: registry.example.com/db:1
    healthcheck:
      test: ["CMD-SHELL", "curl -f http://localhost/health || exit 1"]
      interval: 5s
      timeout: 3s
      retries: 5
"""
    let db = parseComposeProject(source).findService("db").get()
    check db.healthcheck.get().test == @["/bin/sh", "-c", "curl -f http://localhost/health || exit 1"]

  test "rejects a bare test form without a 'CMD'/'CMD-SHELL' prefix":
    let source = """
services:
  db:
    image: registry.example.com/db:1
    healthcheck:
      test: ["true"]
      interval: 5s
      timeout: 3s
      retries: 5
"""
    expect SchemaError:
      discard parseComposeProject(source)

  test "rejects 'NONE' as an unsupported test form":
    let source = """
services:
  db:
    image: registry.example.com/db:1
    healthcheck:
      test: ["NONE"]
      interval: 5s
      timeout: 3s
      retries: 5
"""
    expect SchemaError:
      discard parseComposeProject(source)

  test "rejects 'CMD-SHELL' with more than one argument":
    let source = """
services:
  db:
    image: registry.example.com/db:1
    healthcheck:
      test: ["CMD-SHELL", "curl", "-f"]
      interval: 5s
      timeout: 3s
      retries: 5
"""
    expect SchemaError:
      discard parseComposeProject(source)

  test "rejects a key outside the fixed MVP boundary (networks:)":
    let source = """
services:
  web:
    image: registry.example.com/web:1
networks:
  frontend: {}
"""
    expect SchemaError:
      discard parseComposeProject(source)

  test "rejects a depends_on condition other than service_healthy":
    let source = """
services:
  api:
    image: registry.example.com/api:1
    depends_on:
      db:
        condition: service_started
  db:
    image: registry.example.com/db:1
"""
    expect SchemaError:
      discard parseComposeProject(source)

  test "rejects a healthcheck with no test":
    let source = """
services:
  db:
    image: registry.example.com/db:1
    healthcheck:
      interval: 5s
      timeout: 3s
      retries: 5
"""
    expect SchemaError:
      discard parseComposeProject(source)

  test "rejects a healthcheck with an empty test sequence":
    let source = """
services:
  db:
    image: registry.example.com/db:1
    healthcheck:
      test: []
      interval: 5s
      timeout: 3s
      retries: 5
"""
    expect SchemaError:
      discard parseComposeProject(source)

  test "rejects healthcheck.retries <= 0":
    for badRetries in ["0", "-1"]:
      let source = """
services:
  db:
    image: registry.example.com/db:1
    healthcheck:
      test: ["CMD", "true"]
      interval: 5s
      timeout: 3s
      retries: """ & badRetries & "\n"
      expect SchemaError:
        discard parseComposeProject(source)

  test "rejects healthcheck.interval <= 0":
    let source = """
services:
  db:
    image: registry.example.com/db:1
    healthcheck:
      test: ["CMD", "true"]
      interval: 0s
      timeout: 3s
      retries: 5
"""
    expect SchemaError:
      discard parseComposeProject(source)

  test "rejects healthcheck.timeout <= 0":
    let source = """
services:
  db:
    image: registry.example.com/db:1
    healthcheck:
      test: ["CMD", "true"]
      interval: 5s
      timeout: 0s
      retries: 5
"""
    expect SchemaError:
      discard parseComposeProject(source)
