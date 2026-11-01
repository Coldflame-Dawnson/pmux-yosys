# A independent audit: unified four-rule default path

## Decision

`AUTHORIZED`

This authorization is bound only to B commit
`dbb21ecd5d50d5636aec7415d9a8e6adb9b5f300`. It does not authorize a later
commit by ancestry, branch name, or similarity. It authorizes the next final-G3
or unified-default integration stage; this audit does not perform any merge.

## Object and independence

- Remote branch: `feature/b-v02-unified-case12-default`
- Remote value observed before work:
  `dbb21ecd5d50d5636aec7415d9a8e6adb9b5f300`
- Direct parent: `c39f54e7d3a99b016fe4be8ce4dcb0ec261f9c83`
- Comparison base: `4c43ab9743d69ee6fe28f965e10163b15bdb5c88`
- A branch: `feature/a-v02-unified-case12-default-audit-dbb21ec`
- Worktree: a new Linux-filesystem worktree; the previous A worktree and its
  evidence were not altered.
- Official gates, both 1000-seed runs, and all EQY jobs were regenerated against
  the exact target commit. No B-generated gate or old random result was accepted
  as A evidence.

## Static audit

The range from the authorized base to the target changes 13 files. Production
changes are confined to `pmux_opt.cc`, `run_synth_intel_pmux.py`, and
`synth_intel.cc`; the remainder is B documentation, validation code, and
evidence. `git diff --check` is clean.

The following frozen paths have a zero diff from the comparison base:

- `pmux_project/analyzer/`
- `pmux_project/include/`
- `pmux_project/pass/case12_rewrite.cc`
- `pmux_project/pass/case12_rewrite.h`
- `pmux_project/pass/case34_rewrite.cc`
- `pmux_project/pass/pmux_profile_authorization.h`

An independent production-path scan found no dispatch on official case/module/
file/signal/automatic-cell identity, official input hashes, or fixed case3/case4
vectors. Rule-family labels `case12` and `case34` are configuration names, not
design identity tests.

## Default and rollback matrix

All 20 independent dispatch tests passed.

| Entry or option | Observed behavior | Result |
|---|---|---|
| plain `pmux_opt` | case12, then case34, then generic analysis | PASS |
| `-v02-case12` | same algorithm as default | PASS |
| `-no-v02-case12` | case12 off, case34 on | PASS |
| `-no-v02-case34` | case12 on, case34 off | PASS |
| both disable flags | both structural families off | PASS |
| enable/disable in either order | rejected before write | PASS |
| four stub modes | no structural rewrite | PASS |
| decision-proof mutation | no structural rewrite | PASS |
| native default/compatibility form | identical official gates | PASS |
| native `-no-pmux-v02-case12` | case12-only rollback | PASS |
| native `-no-pmux-v02` | complete structural rollback | PASS |
| runner default/rollback | matches native path | PASS |
| max10/cycloneive/cyclone10lp | no Cyclone IV primitive introduced | PASS |
| repeated/partial/alternating invocation | no switch/profile leakage | PASS |

## Proof boundaries and negative tests

### Malformed `$eq` width matrix

All eight mutations produced zero case12 candidates and zero writes with stable
reason `eq_port_width_mismatch`. No case34 or generic analyzer ran afterward.

| Mutation | Result |
|---|---|
| A_WIDTH differs from A port | PASS |
| B_WIDTH differs from B port | PASS |
| Y_WIDTH differs from Y port | PASS |
| missing A_WIDTH | PASS |
| missing B_WIDTH | PASS |
| missing Y_WIDTH | PASS |
| legal candidate plus malformed noncandidate | PASS; whole batch rejected |
| malformed noncandidate only | PASS; whole batch rejected |

The no-plugin control reproduced the known Yosys 0.69 `check` crash with return
code 139. The PMUX experiment did not call `check`/`stat` on malformed metadata:
it asserted zero writes and then used test-only `design -reset`, exiting normally.
The control crash is therefore not attributed to the PMUX implementation.

### Structural, fanout, priority, protection, and budget boundaries

- B structural fixtures passed 8/8 in default and explicit compatibility modes.
- A structural fixtures passed 15/15.
- A fanout fixtures passed 4/4.
- Five-branch priority and internal-four-layer false positives were rejected.
- Module-output, unknown-port, and uncovered-to-be-deleted-PMUX fanout were
  rejected.
- A legal third PMUX consumer was preserved and reported
  `proved_with_preserved_decode_fanout`.
- All 256 overlapping priority masks preserved priority semantics.
- Protection attributes and all three budget boundaries retained conservative
  zero-write behavior.

## Authorization lifecycle

The stronger symmetric native/runner lifecycle matrix passed 26/26. Each
consumer covered valid first use; LUT parameter, port, attribute and type
mutation; target add/remove; same-name reconstruction; absent snapshot; forged
generation; monitor unregister; and consumed-generation replay. Valid first use
selected `accepted_constant_table`; every stale or mutated case selected
`stock`. Replay selected `[accepted_constant_table, stock]`. Scratchpad state was
consumed before mapping side effects.

The pre-existing static authorization checks and the 14/14 snapshot/generation
matrix also passed.

## Regression results

| Scope | Result |
|---|---|
| Analyzer fast | 2116 / 29 / 60 / 50 / 81 assertions PASS |
| Analyzer slow | 2118 / 29 / 60 / 50 / 81 assertions PASS |
| production mutation seam | 26/26 PASS |
| independent consumer mutations | 13/13 plus ordered 12/12 PASS |
| read-only case3/case4 proof reports | RTLIL fingerprints unchanged |
| 19 real microtests | 19/19 synthesis, structure and EQY PASS |
| default fixed seeds | 1000/1000 synthesis, structure and EQY PASS |
| compatibility explicit fixed seeds | 1000/1000 synthesis, structure and EQY PASS |

## Official four-case independent acceptance

The current `synth_intel.cc` was source-loaded and built into the native audit
entry. This is the target source path, not stock `synth_intel` plus a manually
inserted pass. Native and runner paths produced identical default gates; default
and compatibility-explicit modes also produced identical gates.

Official source SHA-256 values were independently checked before generation:
case1 `2d2dba011b0e8799d0de3790b69b7f01c6eb6ac665c4321b57b0378a4972fd13`,
case2 `e43c9b437071488551caf3121e227e86f4fe3190751f245beac3b7ecc3fdb146`,
case3 `1551b9212622ccef6019d6e77de2c43dc8c80d267957b55a42121f34a08780e9`,
and case4 `c0c5042928bd6bf568e2b7fb2193f2b6db9c4193ba3a5378fc50a331d43422d7`.

| case | post-ABC9 `$lut` | same-stage `cycloneiv_lcell_comb` | DFF | map_cells result | top port bits / IO cells | profile | whole-design EQY |
|---|---:|---:|---:|---|---|---|---|
| 1 | 182 | 0 | 222 | 180 lcell + 2 `$not` | 285 / 0 | stock | PASS |
| 2 | 0 | 15 | 0 | 15 lcell | 30 / 0 | stock | PASS |
| 3 | 11 | 0 | 12 | 10 lcell + 1 `$not` | 12 / 0 | stock | PASS |
| 4 | 27 | 0 | 10 | 26 lcell + 1 `$not` | 18 / 0 | accepted_constant_table | PASS |

Full structural rollback restored 288/25/13/36 `$lut` and 222/0/17/10 DFF.
Case12-only rollback retained case3 11/12 and case4 27/10. IO-cell delta was zero
for all cases. Case2's 15 vendor primitives are not reported as 15 `$lut`.

Measured native default resources (wall/user/system seconds, peak MiB, ABC
seconds):

| case | wall | user | system | peak | ABC |
|---|---:|---:|---:|---:|---:|
| 1 | 0.50 | 0.43 | 0.01 | 38.70 | 0.04 |
| 2 | 0.07 | 0.04 | 0.01 | 34.49 | 0.00 |
| 3 | 2.35 | 2.31 | 0.01 | 35.00 | 0.01 |
| 4 | 0.21 | 0.17 | 0.02 | 35.29 | 0.01 |

## EQY configuration review

The disclosed initial B failure came from asymmetric preprocessing and is not
used as acceptance evidence. A regenerated each gate and ran a symmetric
whole-design configuration: official RTL is gold; the complete post-ABC9 gate
is gate; both sides receive `hierarchy -check -top`, `prep`, `memory_map`, and
`async2sync`; the top partition uses `merge *`; SAT and PDR strategies are
enabled. There are no added assumptions, removed ports, relaxed compare points,
or black-box substitutions. The configuration therefore repairs a false
negative without weakening the equivalence obligation. All four workdirs contain
an actual `PASS` marker.

## Missing and bounded material

- `MISSING`: `design/G3双人协作与接口契约.md` is absent from the exact target and
  from every fetched remote branch. No claim is made that it was read. The
  current audit instruction supplied the controlling two-person contract and
  was used as the oracle.
- `MISSING`: `design/PMUX优化技术方案_v0.2.md` is also absent from repository
  history. A separate old workspace copy was read only as background and was
  not treated as exact-commit evidence.
- `NOT COMMITTED`: approximately 1.3 GiB of transient per-seed/EQY workdirs.
  Machine summaries, complete console logs, official stage artifacts, actual
  EQY configs/logs/PASS markers, and hashes are committed instead.
- No result is `UNVERIFIED` among the required technical gates.

The missing repository copies are documentation-packaging defects, not missing
behavioral obligations: every requirement stated in the controlling audit task
was independently exercised. They therefore do not broaden this authorization
or transfer it to any other commit.

## Final authorization

`AUTHORIZED`

允许下一阶段基于 B 提交
`dbb21ecd5d50d5636aec7415d9a8e6adb9b5f300`
继续最终 G3 判定或统一默认路径集成。不得把本结论用于其他提交；本任务未执行合并。
