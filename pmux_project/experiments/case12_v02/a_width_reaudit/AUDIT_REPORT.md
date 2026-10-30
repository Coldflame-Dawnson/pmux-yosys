# A third independent audit: case1/case2 width re-audit

Date: 2026-10-30

Audit branch: `feature/a-v02-case12-width-reaudit-4c43ab9`

Audited B commit: `4c43ab9743d69ee6fe28f965e10163b15bdb5c88`

Previous blocked B commit: `da516119e0f59e40d678ead0014a8dda83b78366`
Conclusion: **AUTHORIZED**

## 1. Scope and source integrity

The remote branch was read before creating this independent branch. It resolved exactly to the required B commit. The diff from the previously blocked commit changes only:

1. `pmux_project/analyzer/case12_proof_analyzer.cc`
2. `pmux_project/pass/case12_rewrite.cc`
3. `pmux_project/pass/case12_rewrite.h`
4. `pmux_project/pass/pmux_opt.cc`
5. `pmux_project/experiments/case12_v02/scripts/run_case12_validation.py`
6. `pmux_project/experiments/case12_v02/README.md`

No official RTL, frozen interface, case3/case4 proof contract, test expectation, or A audit oracle changed. Static review found no dispatch based on case number, source filename, module name, source location, or automatic cell name.

## 2. Width-metadata fix

`run_case12_structural_rewrite()` now scans every `$eq` in every selected module before creating a case12 analyzer or candidate. Missing or inconsistent `A_WIDTH`, `B_WIDTH`, or `Y_WIDTH` sets `malformed_cell_metadata`, records `eq_port_width_mismatch`, and returns an empty result. `pmux_opt` then clears constant-table authorization and returns before case3/case4 rewrite, the generic analyzer, or any mapping side effect. The malformed batch reports `writes=0`.

Independent matrix results:

| Variant | paired/priority/committed/removed/added | reason | writes | later analyzer |
|---|---|---|---:|---|
| A width mismatch | 0/0/0/0/0 | `eq_port_width_mismatch` | 0 | skipped |
| B width mismatch | 0/0/0/0/0 | `eq_port_width_mismatch` | 0 | skipped |
| Y width mismatch | 0/0/0/0/0 | `eq_port_width_mismatch` | 0 | skipped |
| missing A width | 0/0/0/0/0 | `eq_port_width_mismatch` | 0 | skipped |
| missing B width | 0/0/0/0/0 | `eq_port_width_mismatch` | 0 | skipped |
| missing Y width | 0/0/0/0/0 | `eq_port_width_mismatch` | 0 | skipped |
| legal candidate plus malformed noncandidate | 0/0/0/0/0 | `eq_port_width_mismatch` | 0 | skipped |
| malformed noncandidate only | 0/0/0/0/0 | `eq_port_width_mismatch` | 0 | skipped |

Every PMUX rejection experiment exited normally after `design -reset`. The control experiment, without loading the plugin, reproduced the known Yosys 0.69 `check` crash with exit code 139. That crash is a Yosys malformed-design limitation and is not attributed to the PMUX rewrite.

## 3. Negative and positive structure tests

- B structural suite: 8/8 PASS.
- A independent structural suite: 15/15 PASS.
- Five-branch priority chain: zero candidate and zero rewrite.
- Comparator output exposed as a module output: zero candidate and zero rewrite.
- Unknown cell port and uncovered port on a removed PMUX: conservative rejection.
- Legal third-PMUX consumer: accepted, left connected, and proof reason is `proved_with_preserved_decode_fanout`.
- Priority implementation masks `E4A0/00D8/00D8`: exhaustive 256-vector check, zero mismatches; overlapping selects preserve priority and do not assume mutual exclusion.

Official insertion-point regeneration from the official RTL produced:

| Case | paired | priority | committed | removed | added | `check` |
|---|---:|---:|---:|---:|---:|---|
| case1 | 1 | 0 | 1 | 2 | 18 | PASS, 0 problems |
| case2 | 0 | 1 | 1 | 4 | 15 | PASS, 0 problems |

Default read-only fingerprints remained identical before and after:

- case1: `f7c05d8ddb021cce2d3b24b80cf9ded8b872757281b1e2390f05ec406419da4a`
- case2: `452b85e4a0dd529e8f13579fca3e813a0854f6257b5309931f8ce093036e5f0e`

## 4. Regression results

| Test | Result |
|---|---|
| analyzer fast | 2116 / 29 / 60 / 50 / 81 assertions PASS |
| analyzer slow | 2118 / 29 / 60 / 50 / 81 assertions PASS |
| production decision mutation seam | 26/26 PASS |
| snapshot/generation/lifecycle | 14/14 PASS |
| default-entry fixed seeds | 1000/1000 synthesis, structure, EQY PASS |
| explicit case12 fixed seeds | 1000/1000 synthesis, structure, EQY PASS |

The first random attempt used a relative `--bin` path and failed before loading any design. Those rows are retained as `NOT_RUN` process evidence and are excluded from the result. The retry used an absolute tool path and is the counted 1000/1000 result.

## 5. Unified official four-case result

All gates were regenerated from the four official RTL files using the same path: explicit case12 plus default accepted case3/case4 rules. No B-generated gate was reused.

| Case | post-ABC9 / vendor count | DFF | whole-design EQY | CPU s | peak MB |
|---|---:|---:|---|---:|---:|
| case1 | 182 `$lut` | 222 | PASS | 0.44 | 35.36 |
| case2 | 15 `cycloneiv_lcell_comb` and 0 `$lut` | 0 | PASS | 0.05 | 31.29 |
| case3 | 11 `$lut` | 12 | PASS | 2.39 | 32.09 |
| case4 | 27 `$lut` | 10 | PASS | 0.19 | 32.69 |

case2 is already expressed in Cyclone IV vendor primitives at the post-ABC9 observation point. Its 15 cells must not be reported as 15 `$lut`. The independent whole-design EQY uses a minimal formal model whose LUT index is `{datad, datac, datab, dataa}`.

The dedicated case3/case4 regression also independently regenerated stock case1/case2 and accepted case3/case4 results: 288/25/11/27 `$lut`, with all four whole-design EQY checks PASS. This confirms the accepted case3/case4 path did not regress.

## 6. Toolchain

- Yosys: 0.69, commit `9f75ca1f9834a39a863915b5dae0c7b1e33533bc`
- EQY: v0.69, commit `4a72eb94fc253062464afee4d0018359017bb846`
- SBY: binary v0.70, source commit `b1a1e98cba941ec8433f8dc27f416cd7bb7f14be`
- ABC: commit `a51bf4a7bb38ce06ea3a544b1463b40832213917`
- `pmux_opt.so` SHA-256: `257314235fd91e662907b2f09701c8e81da6d4057f91992c2376816a48b4b11b`

Executable SHA-256 values are recorded in `toolchain.txt`.

## 7. Authorization

**AUTHORIZED**

允许下一阶段基于 B 提交
`4c43ab9743d69ee6fe28f965e10163b15bdb5c88`
继续最终验收或统一默认路径集成。

This authorization is limited to the audited commit and evidence above. It does not authorize changing official RTL, frozen interfaces, test oracles, or enabling a different unreviewed commit.
