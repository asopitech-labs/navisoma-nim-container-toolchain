# Gate decision — Issue #13

## Outcome

**revise the common semantic contract.**

This is #13's own final decision, recorded here and in #13's closing
comment. It is not a new investigation and does not open a new
verification Issue.

## Basis

#13's question was whether a common, backend-neutral Canonical Model /
Execution Graph — something NAVISOMA itself would have to design, own, and
maintain — is necessary to preserve one Compose intent, one user operation
order, and one failure contract across containerd and WSLC, or whether
existing Compose/runtime/adapter tooling composed together already
provides this without it.

- [#14](semantic-core-gate.md) fixed three Compose scenarios and lowered
  them, on paper, to both backends' concepts.
- [#15](wslc-capability-gate.md) then called the real WSLC C API on a live
  host to check what #14 required. It produced genuine WSLC adapter
  capability evidence — lifecycle, cleanup, same-session container
  connectivity, a named volume, a published port, process I/O and
  termination, and consumption of a digest-verified image archive.

That evidence shows what a WSLC adapter can rely on. It does not, by
itself, show that this reliance can only be organized through a
NAVISOMA-owned Canonical Model/Execution Graph rather than through some
combination of existing tools. In particular, #14's `network.named` row
requires a project-scoped network resource with its own create/remove
lifecycle; WSLC's C API has no such object at all (`wslcsdk.h` exposes
only a per-container `BRIDGED`/`NONE` mode flag, confirmed by reading the
full header). #15's probe evidence for that row is therefore narrower than
the row's own required shape — it shows two `BRIDGED` containers in one
session can reach each other, not that a named, independently
lifecycle-managed network resource exists to model in the first place.
That gap was not closed and is not being closed by further probing: it is
a sign that the Canonical Model's assumed resource shape does not hold
uniformly across the fixed backends, which is a reason to revise the
contract, not a reason to gate one more capability.

Taken together, #14/#15 demonstrate WSLC adapter capability and candidate
Canonical Model shapes, but do not establish that this capability could
not be organized without a NAVISOMA-owned semantic core — or, where a
gap like the one above exists, that the currently-drafted contract is the
right shape to carry forward as-is. Neither result is sufficient grounds
to begin production bridge/binding implementation.

## Disposition

- **#13**: closed, outcome `revise the common semantic contract`.
- **#14**: left as-is; its evidence remains part of the basis above.
- **#15**: closed without further verification. The WSLC adapter
  capability evidence it already collected (see "WSLC adapter capability
  evidence" in [`wslc-capability-gate.md`](wslc-capability-gate.md)) is
  retained as-is. Items it did not observe — including the full
  `network.named` resource-with-lifecycle shape discussed above — are not
  filled in.
- No new verification Issue was opened as part of this closure.
