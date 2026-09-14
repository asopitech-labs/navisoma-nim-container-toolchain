# Project continuation decision — Issue #17

## One-screen summary

- #13 closed as `revise the common semantic contract`: a candidate
  backend-neutral Canonical Model/Execution Graph could be described, but
  was not established as irreducible to existing Compose/runtime/adapter
  combinations, and one of its assumed resource shapes
  (`network.named` as a project-scoped resource with its own
  create/remove lifecycle) does not hold on WSLC at all
  ([`gate-13-decision.md`](gate-13-decision.md)).
- #16 closed as: none of the three selected tool families — Docker
  Compose, containerd/nerdctl, or the WSLC SDK family — executes the
  fixed `depends_on: condition: service_healthy` case as an end-to-end
  health gate against both containerd and WSLC under the same contract
  (Docker Compose has its own health gate, but reaches only the Docker
  Engine API, never containerd's or WSLC's own client API) — but that
  absence does not by itself establish that NAVISOMA must own this as an
  irreducible, non-substitutable core; it is recorded as an integration
  responsibility that can be built on top of generic exec/lifecycle
  primitives
  ([`irreducible-core-counterfactual.md`](irreducible-core-counterfactual.md)).
- Reading #1–#12 against those two closed decisions: every issue that
  assumes NAVISOMA directly owns Compose semantic processing, a
  canonical/execution-graph layer, or a cross-platform semantic model
  states that ownership as a premise in its own text. Neither #13 nor
  #16 supplies evidence for that premise — #13 explicitly asked the
  question and did not answer it "yes," and #16 is one concrete instance
  where the candidate for NAVISOMA-owned code was found to be
  reducible to generic primitives plus integration work.
- No issue in #1–#12, and no closed validation document (#13–#16),
  states a single user-facing promise together with (a) the concrete
  backend/workflow contract it must hold under, (b) the existing
  alternatives considered, and (c) an observable semantic or
  failure-contract difference those alternatives cannot meet. That is
  the specific evidence item item 3 of #17's deliverable requires before
  a `narrow` disposition can be written.
- Per #17's own deliverable instructions ("if no such promise is
  evidenced in the repository record, state `stop / pause`"), this
  document records `stop / pause` as the audit's factual finding. Per
  #17's own authority boundary, this is a recorded finding from the
  evidence in the repository, not a product decision — the project owner
  decides `stop / pause` versus `narrow` after review of this table.

## Table: issues #1–#12

| Issue | Premise it assumes | Does #13/#16 support that premise? | Classification | Disposition |
|---|---|---|---|---|
| [#1](https://github.com/asopitech-labs/navisoma-nim-container-toolchain/issues/1) Compose conformance baseline | States directly: "Compose semantic processing is one of the few areas NAVISOMA is expected to own directly." | No. #13 asked exactly whether a NAVISOMA-owned semantic layer (which Compose processing would feed) is necessary at all, and closed `revise`, not confirming ownership. | `blocked by absent product thesis` | Do not begin as scoped. The conformance-baseline research itself could still inform a future `narrow` promise, but its current framing presupposes the ownership question #13 left open. |
| [#2](https://github.com/asopitech-labs/navisoma-nim-container-toolchain/issues/2) Native C/C++ and Nim foundation audit | Assumes NAVISOMA will build production bindings/shims at some point; defines *how*, not *whether*. | Not applicable — does not itself claim an irreducible core. | `conditionally relevant only if one explicit product promise is adopted` | Its reuse-first decision order is independent of which promise is adopted, but committing engineering time to it is conditional on a promise existing to build toward. |
| [#3](https://github.com/asopitech-labs/navisoma-nim-container-toolchain/issues/3) Canonical application model / execution graph | States directly: "This is NAVISOMA-owned code." This is the same Canonical Model/Execution Graph #13/#14 investigated. | No. #13 closed `revise the common semantic contract` against exactly this artifact, and #16 found one concrete candidate piece of it (health-gated scheduling) is not an irreducible core. | `blocked by absent product thesis` | Do not begin. This issue's premise is the specific claim #13 revised and #16 narrowed one instance of. |
| [#4](https://github.com/asopitech-labs/navisoma-nim-container-toolchain/issues/4) containerd API surface / Nim integration boundary | Assumes NAVISOMA needs a production containerd backend integration; the API-surface mapping itself is descriptive research. | Not directly addressed — #13–#16 did not test containerd on a live host at all (#15 tested WSLC only). | `conditionally relevant only if one explicit product promise is adopted` | The API/service dependency mapping is reusable due diligence regardless of outcome, but the "standalone repository" and native-bridge-architecture decisions presume a product scope not yet established. |
| [#5](https://github.com/asopitech-labs/navisoma-nim-container-toolchain/issues/5) WSL Container API C projection / Nim bindings | States: "Validate the WSL Container API as a first-class NAVISOMA backend." | Partially overlapping evidence exists: #15 already produced a C ABI inventory, a capability matrix, and a create/start/exec/stop/delete binding experiment on a live host, closed "without further verification" ([`wslc-capability-gate.md`](wslc-capability-gate.md)). #13/#16 did not conclude WSLC integration must be NAVISOMA-owned rather than a thin adapter over generic primitives. | `conditionally relevant only if one explicit product promise is adopted` | Much of this issue's requested deliverable already exists as #15's adapter capability evidence; the remaining open item is a repository/ownership decision, which is conditional on a promise being adopted. |
| [#6](https://github.com/asopitech-labs/navisoma-nim-container-toolchain/issues/6) OCI reuse strategy | Explicitly reuse-minimizing ("a portable vocabulary does not require reimplementing every OCI library in Nim"); does not itself assert NAVISOMA must own an irreducible OCI layer. | Not applicable — no irreducible-core claim to test against #13/#16. | `generic integration/cost work` | The reuse/ownership audit is useful due diligence independent of the pending product decision; only the final repository-creation decision is conditional. |
| [#7](https://github.com/asopitech-labs/navisoma-nim-container-toolchain/issues/7) BuildKit/LLB integration | Assumes NAVISOMA needs an independent build subsystem integrated with BuildKit; explicitly reuse-first ("NAVISOMA should reuse it and own only the semantic lowering"). | Not directly addressed by #13–#16, which did not investigate `build:` handling beyond #14's Case 3 capability-gate and #15's non-Dockerfile archive-import evidence. | `conditionally relevant only if one explicit product promise is adopted` | The API/LLB mapping research is reusable groundwork; committing to a BuildKit facade is conditional on a promise that needs image building at all. |
| [#8](https://github.com/asopitech-labs/navisoma-nim-container-toolchain/issues/8) Cross-platform runtime capability / platform provisioning model | States the objective as "one NAVISOMA user model" spanning Linux/WSL/macOS/Windows — i.e., another cross-backend semantic layer over containerd and WSLC. | No. This is the same kind of cross-backend semantic-neutrality claim #13 revised; #16 additionally found that at least one candidate piece of cross-backend semantics reduces to generic primitives. | `blocked by absent product thesis` | Do not begin. Restates the unresolved cross-backend-ownership question in platform-provisioning terms. |
| [#9](https://github.com/asopitech-labs/navisoma-nim-container-toolchain/issues/9) Conformance/differential/integration/performance test infrastructure | Test-infrastructure design; several of its listed targets (differential canonical-model comparison, containerd/WSLC/BuildKit backend integration tests) presuppose the components #1/#3/#4/#7 would produce. | Not applicable to the infrastructure design itself; its *targets* inherit the same unresolved premise as the issues above. | `generic integration/cost work` | The test-taxonomy and CI-architecture design is largely independent of which specific components eventually exist; the concrete test suites for still-unbuilt components are conditional. |
| [#10](https://github.com/asopitech-labs/navisoma-nim-container-toolchain/issues/10) Repository extraction policy | Directly conditions each candidate repository ("only if... independently reusable/real owned functionality remains") on decisions #1–#8 have not yet reached. | Not applicable — the issue's own text already makes every strong/conditional candidate contingent. | `conditionally relevant only if one explicit product promise is adopted` | Already self-scoped as conditional; nothing to change, but it cannot produce a repository decision before a promise is adopted. |
| [#11](https://github.com/asopitech-labs/navisoma-nim-container-toolchain/issues/11) Prototype native bridge strategy spike | Targets "representative NAVISOMA dependencies" (WSLC API, containerd/BuildKit-adjacent gRPC/protobuf, JSON Schema) to prove FFI patterns. | Not applicable to the FFI patterns themselves; the specific dependencies chosen presuppose the still-open backend/product questions. | `conditionally relevant only if one explicit product promise is adopted` | The general Nim/C/C++ interop patterns (`importc`/`importcpp`/shim/generated-code) are reusable regardless of outcome, but exercising them against WSLC/containerd/BuildKit specifically is conditional, and #17 forbids new probes/production code in any case. |
| [#12](https://github.com/asopitech-labs/navisoma-nim-container-toolchain/issues/12) Native dependency acquisition/build/redistribution strategy | Generic engineering policy for however many native dependencies end up selected; does not itself assert what NAVISOMA must own. | Not applicable — no irreducible-core claim to test. | `generic integration/cost work` | Useful regardless of which specific dependencies are eventually chosen; not blocked by the open product question. |

## Promise evidence (deliverable item 3/4)

No issue text in #1–#12, and no closed validation document (#13, #14,
#15, #16), states — together, in one place — all of: (a) one concrete
user-facing promise, (b) its target user workflow, (c) the existing
alternatives considered for that exact promise, and (d) an observable
semantic or failure-contract difference those alternatives cannot meet.
The closest candidate in the record is #16's fixed case
(`depends_on: condition: service_healthy` across containerd and WSLC),
and #16's own finding was that no such gap was found among the three
investigated alternatives that would require a NAVISOMA-specific,
non-substitutable core — only an integration responsibility buildable on
generic primitives.

Per #17's deliverable instructions, this document therefore records:

**stop / pause.**

This is a factual finding drawn from the current repository record, over
which #17's authority boundary reserves the actual continuation decision
to the project owner. Every other assessment in this document — the
per-issue classification table and this finding — is offered as
evidence for that decision, not as the decision itself.

## Scope note

This document was produced from the fixed set of closed decisions in
`gate-13-decision.md`, `semantic-core-gate.md`, `wslc-capability-gate.md`,
and `irreducible-core-counterfactual.md`, and from the current text of
Issues #1–#12 and #13–#16. No new probe, host run, implementation, or
capability investigation was performed. No Issue other than #17 was
closed, reopened, or edited as part of producing this document.
