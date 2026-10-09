# A independent re-audit: unified profile authorization consumption

## Verdict

`AUTHORIZED`

A authorizes C to begin final default-path validation of B commit
`621cb4e56b734da73cfb6161b951a375893e2d81`.  This verdict is limited to the
one-shot `pmux_opt.accepted_constant_table` consumption fix and its regression
impact.  It does not replace C's resource or whole-design acceptance.

## Objects and scope

- B branch: `feature/b-v02-unified-selection-gate`
- audited B commit: `621cb4e56b734da73cfb6161b951a375893e2d81`
- blocked predecessor: `9107a466f74ce27269361ee26e99093c1b8fc93e`
- A branch: `feature/a-v02-unified-selection-consume-reaudit-621cb4e`
- A audit content commit: `3cc303df7ea1336659f137b564453aab76c76b66`
- fixed toolchain root:
  `/home/james/work/pmux-yosys/pmux_project/official_baseline/toolchain/install`

`git rev-parse 621cb4e^` returned exactly `9107a466...`; the fix is a direct
child.  Its three-file range is:

- `M pmux_project/experiments/case34_v02b/UNIFIED_SELECTION_GATE_STATUS.md`
- `A pmux_project/experiments/case34_v02b/scripts/check_profile_consumption.py`
- `M techlibs/intel/synth_intel.cc`

The production source delta is two lines in `synth_intel.cc`; it unconditionally
clears the scratchpad after the profile decision and before `techmap`.  No
`pmux_project/pass/` or frozen-interface file differs in the B fix range.

## Source-order audit

At `techlibs/intel/synth_intel.cc:266-274` the execution order is:

1. read `pmux_opt.accepted_constant_table`;
2. scan the current selected design for a `pmux_case34_lut4` cell;
3. retain their conjunction in invocation-local
   `pmux_constant_table_profile`;
4. unconditionally write the scratchpad authorization to false;
5. only then run `techmap` or any subsequent ABC9-side effect.

The clear does not alter the already selected local Boolean, so the first legal
case4 consumption still selects `accepted_constant_table`.  Help mode performs
no real consumption and is outside a real synthesis invocation.

The runner path at `pmux_project/pass/pmux_opt.cc:787-805` has the same order:
current cell scan, scratchpad read and conjunction, unconditional clear, then
mapping effects.  The two integration paths are semantically consistent.

The production `pmux_opt` path also clears stale authorization on entry
(`pmux_opt.cc:523`) and only publishes true after an actually committed
`constant_table_cofactor` edit (`pmux_opt.cc:555-560`).  Disable, no-candidate,
stub, mutation, proof rejection, snapshot failure, conflict, UNKNOWN, and
budget-failure paths therefore cannot publish mapping authorization.

## Independent behavior replay

Command:

```text
make -C pmux_project/analyzer unified-selection-audit \
  YOSYS_ROOT=/home/james/work/pmux-yosys/pmux_project/official_baseline/toolchain/install
```

Result: `17/17 PASS`; the source-order probe reported
`synth_intel_consumes_scratchpad=true`.

The strengthened real replay performs:

```text
pmux_opt                       # legal case4 proof and committed edit
pmux_opt_abc9                  # first consumption
scratchpad -assert ... false
pmux_audit_reintroduce_lut4   # A-only cell reintroduction
pmux_opt_abc9                  # second consumption, no new proof
```

Observed profile sequence:

```text
accepted_constant_table
stock
```

Thus even an actual reappearance of the marker cell cannot replay consumed
authorization.  The other passing behavior cases cover no auth/no cell, forged
cell only, forged auth only, legal case4, direct repeated mapping, explicit
disable, stub, mutation, snapshot mismatch, proof rejection, case1, case2,
case3 decision-cone, compatibility enable, conflict, and stale authorization
at a new `pmux_opt` invocation.

B's supplied model/static checker was also run independently:

```text
python3 pmux_project/experiments/case34_v02b/scripts/check_profile_consumption.py
PROFILE_AUTHORIZATION_CONSUME_REPLAY PASS
```

The A behavior replay, rather than this model alone, is the authorization
evidence.

## Proof-consumer and analyzer regression

- production decision-proof mutation seam: `26/26 PASS`;
- A original consumer mutations: `13/13 PASS`;
- A ordered-structure mutations: `12/12 PASS`;
- analyzer fast: `2116 / 29 / 60 / 50 / 81 PASS`;
- analyzer slow: `2118 / 29 / 60 / 50 / 81 PASS`;
- `synth_intel.cc` fixed-toolchain C++20 syntax check: PASS.

The case3 decision-cone report remains fully proved with usage
`24 cells / 50 bits / 216390 events`; its test consumer reconstructed the DAG
and `DECISION_CONE_NETLIST_UNCHANGED 1`.

Production proof replay remains:

- case3: guarded proof `all=1`, usage `12 / 25 / 195180`, fingerprint unchanged;
- case4: 5-bit address, 10-bit output, 32 rows, row 0 `1001110000`, usage
  `23 / 122 / 7784`, fingerprint unchanged.

The existing analyzer boundary tests cover all three fixed budget failures;
such results never create a committed structural edit and hence cannot publish
the scratchpad authorization.

## Four-case production replay

Command:

```text
python3 pmux_project/experiments/case34_v02b/scripts/run_production_validation.py \
  --repo . \
  --tool-bin /home/james/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/bin \
  --official /home/james/work/pmux-yosys/pmux_project/pmux_case/competition_case \
  --out /tmp/a-consume-reaudit-four
```

| case | structural edit | profile | post-ABC9 | gate EQY |
|---|---:|---|---|---|
| 1 | 0 | stock | 288 LUT / 222 DFF | PASS |
| 2 | 0 | stock | 25 LUT / 0 DFF | PASS |
| 3 | 1 decision-cone | stock | 11 LUT / 12 DFF | PASS |
| 4 | 1 constant-table | accepted_constant_table | 27 LUT / 10 DFF | PASS |

## P1, interfaces, and A modification boundary

The B fix adds no case/module/file/source-location/signal/automatic-cell-name or
fixed-vector identity test.  The structural proof matcher and consumer are
unchanged from the previously reviewed commit; only the authorization lifetime
was shortened.  Frozen headers under `pmux_project/include/` are byte-unchanged
