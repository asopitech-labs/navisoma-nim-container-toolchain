## Public error hierarchy, per docs/architecture.md section 13 ("Error
## model"). Phase 1 defined the pure-semantics layers below; Phase 2
## adds RuntimeError and UnhealthyError for the executor and backend
## port. PlatformError/NativeInteropError etc. still belong to later
## phases that add a real (non-fake) backend.

type
  NavisomaError* = object of CatchableError
    ## Base type for every NAVISOMA-owned public error.

  SchemaError* = object of NavisomaError
    ## The input is not well-formed YAML, or violates the fixed MVP
    ## input boundary (an unsupported key, or a key present with the
    ## wrong shape).

  ComposeSemanticError* = object of NavisomaError
    ## The input is well-formed and within the MVP boundary, but violates
    ## a Compose-level consistency rule, such as `depends_on` naming a
    ## service that is not declared under `services`.

  PlanningError* = object of NavisomaError
    ## The canonical project is internally consistent, but the execution
    ## graph cannot be scheduled, such as a `depends_on` cycle.

  RuntimeError* = object of NavisomaError
    ## A backend port operation (resolve/create/start/stop/remove/exec)
    ## failed. Phase 2 introduces this because the executor now calls a
    ## backend; the backend-native cause is never attached here (#18
    ## error model, "backend-native errors ... never appear in planner
    ## contracts or CLI output") — only a NAVISOMA-level message.

  UnhealthyError* = object of NavisomaError
    ## A dependency reached `unhealthy` (health retries exhausted)
    ## before its dependent could be created/started — #18's semantic
    ## failure contract. Carries only the service name in its message,
    ## never backend detail.
