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

### Negative claims and isolation

An absent marker, empty output, timeout, or failed command is not by itself
evidence of isolation or denial: it can also be a broken probe, a dead target,
or a wrong route. A negative claim must assert all of the following in its
actual pass predicate:

- setup and measurement completion (created/started, wait, and exit-status
  retrieval where applicable);
- the expected negative result (for example, the documented non-zero exit
  status and absence of the target marker);
- a positive control that establishes the named target is live and returns its
  marker through the relevant transport; and
- source/target identity evidence sufficient to rule out self-routing or an
  ambiguous overlapping address.

For an isolation claim, test both directions unless the documented API
contract establishes that the boundary is symmetric. One failed directed
connection proves only that directed attempt, never the broad label
"isolated".

An IP address observed inside a network namespace is not a globally unique
target identity. Renumbering containers until their address strings differ
does not fix an overlapping-address ambiguity: the source namespace can still
route that string to its own container or to no container at all. Use a route
whose endpoint identity is observable from both sides. If the platform exposes
no such route, record the address-space boundary that was observed but leave
cross-project reachability/isolation `unverified`.

### Assertion-to-report traceability

Every stated oracle must map to a single boolean predicate in the probe. A
value printed only in a diagnostic string, or asserted only by a different
CHECK row, does not satisfy that row's oracle. Independent review must trace
each `supported` result from its prose oracle to the exact predicate and its
inputs.

For a required capability, `SKIP` or any other inconclusive result must make
the probe/harness exit non-zero and leave the capability unverified. A run
that exits successfully must not contain a skipped required CHECK.

### External artifact provenance

An artifact claimed to come from a registry or builder must be pinned to an
immutable producer identity (for example, a manifest digest), verify every
declared input blob digest, and record the final artifact digest used by the
run. Repackaging a fetched image proves archive compatibility; it must not be
described as a Dockerfile/BuildKit build unless such a build actually produced
the artifact.

### Cleanup

Every created resource must have one explicit owner and deletion action.
Running twice with the same names proves name reuse only. It does not prove
that caller-owned files, VHDX directories, caches, or external resources were
removed. Report SDK cleanup and caller cleanup separately, then assert both
postconditions when complete cleanup is claimed.

"Complete cleanup" applies to every resource the probe creates, including
supplementary fixtures imported into a runtime and resources in secondary
sessions. A primary capability's cleanup check cannot stand in for a distinct
resource with another identity.

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
