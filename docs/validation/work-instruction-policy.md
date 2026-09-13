# Validation Work-Instruction Policy

## Invariant

A validation result proves only the behavior that the probe observed in its
declared environment. A successful neighboring operation, an API declaration,
or manual cleanup after the probe is not evidence for an unobserved claim.

## Required proof contract

Every validation issue must name, for each claimed capability:

| Field | Required content |
|---|---|
| Claim | One precise behavior, not a broad feature label. |
| Environment | Version, architecture, runtime, and external prerequisites. |
| Stimulus | Exact input and API/CLI operation under test. |
| Oracle | Observable output or state that makes the claim pass or fail. |
| Negative condition | What result would refute the claim. |
| Consumer proof | The next operation that consumes the produced result. |
| Cleanup owner | SDK, NAVISOMA, or caller; include the observable postcondition. |
| Boundary | What this probe explicitly does not prove. |

`supported` is allowed only when every field has evidence. Otherwise use
`capability-gated`, `unverified`, or `scope-breaking` as appropriate.

## Rules for recurring failure modes

### I/O and process behavior

Retrieving stdout/stderr handles does not prove either stream works. A probe
claiming process I/O must emit distinct known markers to both streams, assert
both markers, and assert the expected exit status. A recurring operation must
also prove either a second invocation or the scheduler contract that owns
recurrence.

### Artifact handoff

An artifact handoff is proven only by the complete chain:

```text
producer output -> transfer/import -> exact produced identity -> consumer use
```

For example, a build-image handoff requires importing an externally produced
image and creating/running a container from that exact imported reference.
Tagging a separately pulled image or accepting an import without consuming it
does not prove the handoff.

### Semantic lowering

A proposed mapping between concepts (for example, Compose network to WSLC
session) is a hypothesis until the required semantics are observed. The probe
must state the semantic property it tests, such as membership, connectivity,
isolation, or lifecycle ownership. If no such property is tested, the mapping
remains `capability-gated`; it is not `supported` merely because an API setting
has a similar name.

### Cleanup

Every created resource must have one explicit owner and deletion action.
Running twice with the same names proves name reuse only. It does not prove
that caller-owned files, VHDX directories, caches, or external resources were
removed. Report SDK cleanup and caller cleanup separately, then assert both
postconditions when complete cleanup is claimed.

## Decision and review rules

- A parent gate remains pending while any required capability is unverified.
- A conditional result is reported as `proceed to the next validation`, never
  as product-level `proceed`.
- Before closing an issue, an independent reviewer traces every `supported`
  row to its stimulus, oracle, consumer proof, and cleanup postcondition.
- Review findings that invalidate a claimed row reopen the issue and replace
  its conclusion; they are not append-only caveats.

## Task-author checklist

Before assigning work, write the proof contract first. The worker should be
able to answer all of these without designing the test contract themselves:

1. What exact behavior is being proved?
2. What exact observation makes it pass, and what makes it fail?
3. Which result must a downstream consumer actually use?
4. Which resources are created, who removes each one, and how is absence
   observed?
5. What conclusion is forbidden until this evidence exists?

This policy governs validation work in `docs/validation/` and related issues.
