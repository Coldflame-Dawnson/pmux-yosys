# A re-audit: constant-table authorization lifecycle binding

## Verdict

`BLOCKED`

B commit `ec86209a61f22020707b299f5175f02b8d71495d` fixes the previous
same-name reconstruction failure, but the consumer does not bind the scratchpad
generation string back to the registered lifecycle monitor.  Replacing a valid
generation with an arbitrary nonempty string leaves the content snapshot and
lifecycle Boolean unchanged and is accepted by both consumers.  C must remain
paused.

No EQY `ERROR` or `UNKNOWN` result is described as inequivalence evidence.  The
blocker is a direct authorization-boundary violation.

## Objects and scope

- B branch: `feature/b-v02-unified-selection-gate`
- audited B commit: `ec86209a61f22020707b299f5175f02b8d71495d`
- exact parent: `1ba34ab14077df119cdfa5f9bca8d24c08a24ca6`
- previous A blocked content: `47b430b5605960e0413b010e9a6ba87689684671`
- previous A final HEAD: `d62248d44178da328bab57bf76348326b6285d46`
- A branch: `feature/a-v02-profile-lifecycle-reaudit-ec86209`
- fixed toolchain:
  `/home/james/work/pmux-yosys/pmux_project/official_baseline/toolchain/install`

The B commit is the direct child requested.  Its range contains the lifecycle
helper and the two consumer integrations, tests, README and status document.
Frozen proof APIs and proof obligations are unchanged.  No case/file/top/module/
signal/automatic-cell-name or fixed-vector dispatch was added.  Generation is
post-commit authorization metadata, not a candidate-discovery trigger.

## Lifecycle contract audit

| Obligation | Result | Audit conclusion |
|---|---|---|
| nonempty, nonreused generation per publication | PASS in one publisher lifetime | design address plus monotonic 64-bit serial |
| bind all target instances and modules | PASS | monitor stores sets of every target `Cell*` and owning `Module*` |
| port change or target deletion invalidates | PASS | `notify_connect` invalidates; removal unsets all ports first |
| target module deletion invalidates | PASS by source path | `notify_module_del` checks the stored module set |
| allocator address reuse cannot restore validity | PASS | lifecycle is a one-way false scratchpad state for the active generation |
| identical same-name reconstruction rejected | PASS on both real consumers | payload remains equal, lifecycle becomes false |
| content snapshot remains independently checked | PASS | full type/parameters/attributes/ports/bits/set/count payload comparison remains |
| authorization + generation + lifecycle + target + snapshot required | **FAIL** | generation is checked only for nonempty text, not against a live owned monitor |
| clear all four transient fields before effects | PASS | shared clear runs before either mapping side effect |
| runner and native `synth_intel` share contract | PASS | both call the same inline match/clear helpers |

The failing code path is
`PmuxProfileAuthorization::constant_table_authorization_matches()`: it checks
`generation_key` only with `.empty()`.  It never proves that the supplied token
equals an owned monitor's immutable generation for this design.  The lifecycle
Boolean therefore becomes independently forgeable metadata rather than a
binding to the monitored object set.

## Previous counterexample: independently fixed

A retained its own wrapper around the production `pmux_opt`; it did not use B's
mutation pass.  In both paths A removed one authorized target and reconstructed
the same module/cell name, type, parameters, attributes and complete port map.

Observed:

```text
payload_equal=1
lifecycle_valid=0
profile=stock
reason=target_lifecycle_mismatch
```

The same result was obtained from:

1. the real source-loaded default `synth_intel` path;
2. the `pmux_opt_abc9` runner path.

A also changed an authorized A port and restored the exact original connection.
The content payload again matched, but lifecycle remained false and both paths
selected stock.  Thus lifecycle invalidation is permanent for these events.

## New minimum counterexample

After an otherwise legal publication, A performs only:

```text
scratchpad generation := "A-forged-generation"
```

Authorization remains true, lifecycle remains true, the target set and complete
content snapshot remain unchanged.  The arbitrary string is nonempty but does
not name the registered monitor generation.

Command:

```text
make -C pmux_project/analyzer profile-snapshot-reaudit \
  YOSYS_ROOT=/home/james/work/pmux-yosys/pmux_project/official_baseline/toolchain/install \
  PROFILE_SNAPSHOT_AUDIT_OUT=/tmp/a-profile-lifecycle-reaudit
```

Observed:

| path | required | actual | lifecycle before consume | rejection |
|---|---|---|---:|---|
| source-loaded default `synth_intel` | stock | **accepted_constant_table** | 1 | none |
| `pmux_opt_abc9` runner | stock | **accepted_constant_table** | 1 | none |

Both commands completed successfully.  Both consumers subsequently cleared
authorization, snapshot, generation and lifecycle, but clearing after the local
profile Boolean was computed cannot undo the erroneous authorization.

Control rows passed on both paths: legal first consumption, lifecycle=false,
empty generation, exact same-name reconstruction, and port change followed by
restore.  Empty generation selected stock; only a wrong nonempty generation is
incorrectly accepted.

## Monitor ownership and lifetime

- The publisher owns monitors through `unique_ptr` and registers the raw pointer
  in `Design::monitors`.
- In the same dynamic object, `clear_constant_table_authorization()` erases raw
  registrations before releasing ownership.
- Across dynamic objects, each inline helper has a DSO-local registry.  A native
  `synth_intel` consumer clears the shared scratchpad generation and makes the
  publisher monitor inactive; the publisher removes its owned inactive monitor
  on the next `pmux_opt` clear/publication.
- `active()` compares the monitor's immutable generation with scratchpad state,
  so old monitors do not invalidate a newer generation.
- Repeated publication first clears publisher-owned monitors, preventing an
  accumulating active-monitor set in the normal repeated path.

The source does not provide a consumer query that maps the current generation
back to an owned active monitor.  This is the direct cause of the blocker.  In
addition, cleanup at `Design` destruction without another publisher-side clear
is not explicitly hooked; that ownership edge remains `UNVERIFIED`, but is not
needed to establish the reproducible rejection failure.

## Regression results

- B static lifecycle check: PASS.
- B real lifecycle suite: `12/12 PASS`.
- A independent lifecycle matrix: 12 rows over two paths; 10 expected rows PASS,
  2 forged-generation rows FAIL as the same blocker.
- production decision mutation seam: `26/26 PASS`.
- A decision-consumer mutations: `13/13 PASS`.
- A ordered-structure fixture: `12/12 PASS`.
- analyzer fast: `2116 / 29 / 60 / 50 / 81 PASS`.
- analyzer slow: `2118 / 29 / 60 / 50 / 81 PASS`.
- real micro suite: `19/19 PASS`, including structure/check/EQY/semantic.

Normal four-case production replay:

| case | baseline LUT/DFF | default LUT/DFF | profile | whole-design EQY |
|---|---:|---:|---|---|
| case1 | 288/222 | 288/222 | stock | PASS |
| case2 | 25/0 | 25/0 | stock | PASS |
| case3 | 13/17 | 11/12 | stock | PASS |
| case4 | 36/10 | 27/10 | accepted_constant_table | PASS |

Normal regression success cannot authorize the generic forged-generation case.

## Required correction

The shared consumer must prove that the scratchpad generation corresponds to a
currently owned, registered monitor for this exact design and that this monitor
is the one whose immutable target instance set remains valid.  A merely nonempty
or caller-supplied string is insufficient.  Success and all rejection paths must
continue clearing all four transient fields before side effects.  The two A
forged-generation rows must then select stock.

A changed only `pmux_project/analyzer/`.  It did not modify production code,
frozen APIs, official RTL, B tests, or expected results.

Final decision: `BLOCKED`.
