## Public error hierarchy, per docs/architecture.md section 13 ("Error
## model"). Only the layers Phase 1 (pure semantics) can raise are defined
## here; RuntimeError/PlatformError/NativeInteropError etc. belong to later
## phases that add a backend.

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
