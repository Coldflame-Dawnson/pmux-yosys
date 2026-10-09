# A re-audit: constant-table authorization snapshot binding

## Verdict

`BLOCKED`

B commit `1ba34ab14077df119cdfa5f9bca8d24c08a24ca6` must not enter a new C final
acceptance.  The snapshot detects value/shape mutations covered by B's tests,
but it does not bind the lifetime of a target object: deleting a target cell and
recreating an exact same-name, same-content cell produces an identical payload
and incorrectly retains `accepted_constant_table` authorization.

This report does not interpret C's earlier mutation EQY `ERROR` as an
inequivalence proof.  The blocker is independently established at the mapping
authorization boundary and requires no EQY claim.

## Audited object and branch

- B branch: `feature/b-v02-unified-selection-gate`
- audited B commit: `1ba34ab14077df119cdfa5f9bca8d24c08a24ca6`
- direct parent: `621cb4e56b734da73cfb6161b951a375893e2d81`
- C blocker evidence: `1de7aaf96b3957f2ed54c7c6ca509b00c39d74a2`
- A branch: `feature/a-v02-profile-snapshot-reaudit-1ba34ab`
- fixed toolchain:
  `/home/james/work/pmux-yosys/pmux_project/official_baseline/toolchain/install`

The parent relationship is exact.  The B range changes only the shared
authorization helper, its two consumers/publisher, build/readme integration,
snapshot tests, and status documentation.  Frozen proof headers and existing
proof obligations are unchanged.

No new case/file/top/module/signal/automatic-cell-name or fixed-truth-vector
trigger was found.  Actual module and cell names are used only as fields in an
authorization snapshot after the proof-gated edit commits; they do not select a
candidate or enable a rule.  That post-commit object binding use is legitimate,
but its current definition is incomplete as described below.

## Field-by-field snapshot audit

The implementation is `pmux_project/pass/pmux_profile_authorization.h`.

| Required binding | Result | Evidence |
|---|---|---|
| all target cells | PASS | traverses every design module and every `pmux_case34_lut4` |
| module identity | PASS for serialized name | length-prefixed module `IdString` |
| cell identity | **FAIL for object lifetime** | only cell name/content is serialized; no publication-instance token |
| cell type | PASS | length-prefixed type field |
| parameter set | PASS | count plus sorted names |
| parameter flags/width/value | PASS | flags, width, and full `0/1/x/z` bit string |
| attribute set and values | PASS | count, sorted names, flags, width, full bit string |
| complete ports | PASS | count plus sorted connection names |
| connection order | PASS | `SigSpec` bits serialized in physical order |
| wire/constant bits | PASS | wire name and offset, or exact RTLIL constant state |
| target multiplicity | PASS for count/name set | final count is serialized |
| deterministic order | PASS | modules, cells, parameters, attributes and ports are sorted |
| concatenation ambiguity | PASS | variable strings use length prefixes; collections carry counts |
| zero target | PASS | publication and matching reject zero cells |
| missing/empty snapshot | PASS | matching rejects empty payload |
| Boolean-only authorization | PASS | missing snapshot rejects |

`RTLIL::Const::as_string()` preserves all per-bit `0/1/x/z` states and width is
separately encoded.  Additions, removals and field changes covered by B's suite
therefore change the payload.  However, a delete followed by an exact
same-name/same-content reconstruction leaves every serialized field and the
cell count unchanged.  The explicit requirement that object replacement must
invalidate authorization is consequently not met.

## Reproducible minimum counterexample

A's independent fixture wraps the real proof-producing `pmux_opt`, then:

1. captures the published full snapshot;
2. selects one generated target cell;
3. copies its type, parameters, attributes and complete port map;
4. removes the original cell;
5. creates a new cell under the same module and same `IdString`;
6. restores every copied field without changing the surrounding netlist;
7. invokes the real consumer.

Command:

```text
make -C pmux_project/analyzer profile-snapshot-reaudit \
  YOSYS_ROOT=/home/james/work/pmux-yosys/pmux_project/official_baseline/toolchain/install
```

Observed for both consumption paths:

| path | mutation | required | actual | payload equality |
|---|---|---|---|---|
| real default `synth_intel` | none | accepted_constant_table | accepted_constant_table | n/a |
| real default `synth_intel` | same-name rebuild | stock | **accepted_constant_table** | equal |
| `pmux_opt_abc9` runner | none | accepted_constant_table | accepted_constant_table | n/a |
| `pmux_opt_abc9` runner | same-name rebuild | stock | **accepted_constant_table** | equal |

Both commands returned zero, both scratchpad fields were consumed and cleared,
and the fixture reported:

```text
A_PROFILE_SNAPSHOT_SAME_NAME_REBUILD_COUNTEREXAMPLE REPRODUCED
```

The first possible mapping side effect follows the erroneous successful match;
in `synth_intel` it is the common `techmap`, and in the runner it is the
constant-table lowering `techmap`.  The wrong custom-profile decision has
already been stored locally before either point, so later clearing does not
repair the authorization error.

## Publication and consumption order

Apart from the blocker, the ordering contract is implemented correctly:

- publication first clears both fields and publishes only for a nonempty target
  set after a committed `constant_table_cofactor` edit;
- both consumers rebuild the actual snapshot through the shared helper;
- both require Boolean authorization, a nonempty expected snapshot, a nonzero
  target count and byte equality;
- both clear Boolean and snapshot before mapping effects, on success and
  mismatch;
- a mismatch lowers current custom cells so stock mapping can continue and does
  not select the custom ABC9 script;
- normal post-consumption cell replay is rejected because authorization is
  already empty.

The two paths are semantically consistent; the shared incomplete identity
definition makes both fail the same mandatory replacement test.

## Test results

- B static/model check: `PROFILE_AUTHORIZATION_SNAPSHOT_CONSUME_REPLAY PASS`.
- B real snapshot suite: `10/10 PASS`.
- A same-name object replacement: reproduced on both paths; mandatory expected
  profile failed (`stock` required, `accepted_constant_table` observed).
- production decision-proof mutation seam: `26/26 PASS`.
- A original decision-consumer fixture: `13/13 PASS`.
- A ordered-structure fixture: `12/12 PASS`.
- analyzer fast: `2116 / 29 / 60 / 50 / 81 PASS`.
- analyzer slow: `2118 / 29 / 60 / 50 / 81 PASS`.
- real micro suite: `19/19 PASS`, including structure/check/EQY/semantic.

The broader requested snapshot mutation matrix cannot be marked “all PASS”:
the mandatory same-name replacement row fails in two independent real paths.
The B suite independently confirms LUT bit/width, port, attribute, type,
target removal/addition, missing snapshot and consumed replay behavior, but it
does not cover the failing replacement equivalence class.

## Four official cases

Normal, unmutated default-path replay remains:

| case | baseline LUT/DFF | default LUT/DFF | profile | whole-design EQY |
|---|---:|---:|---|---|
| case1 | 288/222 | 288/222 | stock | PASS |
| case2 | 25/0 | 25/0 | stock | PASS |
| case3 | 13/17 | 11/12 | stock | PASS |
| case4 | 36/10 | 27/10 | accepted_constant_table | PASS |

These normal results do not authorize the incomplete generic snapshot contract.
No EQY result was used to excuse or infer away the counterexample.

## Required B fix and boundary

B must add a publication-time identity/lifetime binding that cannot be
reconstructed merely by deleting a target and recreating the same serialized
name/content, bind it for every target object, and validate it in the shared
consumer before any side effect.  Both success and failure must still consume
all authorization state.  A then needs the same two-path counterexample to
produce `stock`, plus the complete mutation and regression suites.

A added only files and a test target under `pmux_project/analyzer/`.  A did not
modify `pmux_project/pass/`, `techlibs/intel/`, frozen interfaces, official RTL,
B tests, or expectations.

Final decision: `BLOCKED`.
