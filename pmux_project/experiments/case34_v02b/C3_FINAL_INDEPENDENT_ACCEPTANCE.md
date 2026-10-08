# C final independent acceptance: case3 11/12 and case4 27/10

Date: 2026-10-08. Reviewer: role C. This report records fresh runs against B commit `25217ab0b901c27a2ee9e56bd38c67819c19978f`; B's PASS files were not used as verdicts. C changed only the test drivers, this report, and evidence files. Production analyzer, Pass, official RTL, frozen environment and test expectations were not edited.

## Provenance and scope

- Source branch: `feature/b-v02-case34-production`, exact B HEAD `25217ab0b901c27a2ee9e56bd38c67819c19978f`.
- C worktree: `/home/yuewen/work/pmux-yosys-c-v02-final`; C branch: `feature/c-v02-case34-final-acceptance`. Parent chain: `b01a9f406` → `5815dd0d5ddb0791dc059a592f02eceefd634d59` → `2c4908e2e705c525a73ac49d128a27ec3735c129` → `25217ab0b901c27a2ee9e56bd38c67819c19978f`. Origin: `https://github.com/Coldflame-Dawnson/pmux-yosys.git`.
- B range `b01a9f406..25217ab0b` touches the proof analyzer, proof API, Pass consumer, build files, status/validation documentation. `git diff --name-only` contains no `test1.v`–`test4.v`, frozen toolchain, official scoring expectation, or public Pass interface replacement. C's final commit file list should contain only the two `c_v02_*.py` drivers, this report and `c_final_evidence/`.
- Official RTL SHA-256: test1 `2d2dba011b0e8799d0de3790b69b7f01c6eb6ac665c4321b57b0378a4972fd13`; test2 `e43c9b437071488551caf3121e227e86f4fe3190751f245beac3b7ecc3fdb146`; test3 `1551b9212622ccef6019d6e77de2c43dc8c80d267957b55a42121f34a08780e9`; test4 `c0c5042928bd6bf568e2b7fb2193f2b6db9c4193ba3a5378fc50a331d43422d7`.
- Frozen binaries: Yosys 0.69 (`9f75ca1f9`), EQY 0.69, SBY 0.69, ABC 1.01. Clean rebuilt analyzer `.so` SHA-256 `fd09099374a4095fe0df36defe1d13a2ea2c653764ab8ffeae22e32f3606508e`; `pmux_opt.so` SHA-256 `d88f787e0fa580bca520766df2d464acf466671a67133bf7803a5bdb2e4217`.

## Independent runs

- Clean analyzer/Pass builds: PASS. Analyzer `make test` and `make test-slow` both PASS, each with 2116/29/60/50/81 assertions. Case3, case4, production case3/case4 and decision-cone proof reports PASS; report `NETLIST_UNCHANGED 1`, `DECISION_CONE_NETLIST_UNCHANGED 1`, and `PRODUCTION_PROOF_NETLIST_UNCHANGED 1`. The proof for test3 reports source width 8, support 7, one fixed bit, 3 comparators, all obligations true, `reason=proved`; the independent consumer reconstruction reports 3 constant roots and 2 shared root groups. No A `BLOCKED` result was observed in this tested revision. This C result does not override any later A proof-boundary decision.
- Default-mode micro suite: 19/19 PASS (structure, Yosys `check`, whole-design EQY and semantic checks). Test-only snapshot abort: PASS, zero structural write after abort, `check` and EQY PASS.
- Fixed seed SHA-256 `8db91b2ee25d579493dbc2ca66417cc945e215b5424349884013834d43df7ac4`. The ordinary default Pass path: 1000/1000 real synthesis, structure and EQY PASS. Then C's own adapter invoked `pmux_opt -v02-case34`: 1000/1000 real synthesis, structure and EQY PASS. The explicit run found zero structural-match seeds. All 1000 plans have `structural_edits=[]`, `changed_structures=0`, reason `no_v02_case34_candidate`; hence no unproved random structural rewrite was committed. Failure seeds: none.
- C independently synthesized all four official RTL files in isolated baseline, explicit v0.2/default ABC, and explicit v0.2/custom ABC directories. Each of 12 post-ABC9 gates passed Yosys `check` and separate official-gold whole-design EQY. Each directory contains `run.ys`, Pass `plan.json`/`stats.json`, post-ABC9 JSON/RTLIL, Yosys log, `.eqy`, EQY log and work `PASS`. This is C's run, not a copied B gate/PASS.

## Resources and timing

These counts are from **post-ABC9 top JSON**: `$lut`, `dffeas`, IO port bits and total cells. CPU is Yosys user+system seconds; ABC is the reported ABC elapsed seconds; memory is Yosys peak MB. They are not Intel vendor primitive counts. Each row has `check=PASS` and official RTL whole-design `EQY=PASS`.

| case | variant | `$lut` | `dffeas` | IO in/out | total cells | CPU s | ABC s | peak MB | EQY |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| 1 | baseline | 288 | 222 | 259/26 | 510 | 0.11 | 0.03 | 35.53 | PASS |
| 1 | v0.2 default ABC | 288 | 222 | 259/26 | 510 | 0.13 | 0.03 | 35.95 | PASS |
| 1 | v0.2 custom ABC | 288 | 222 | 259/26 | 510 | 0.12 | 0.09 | 35.52 | PASS |
| 2 | baseline | 25 | 0 | 25/5 | 25 | 0.02 | 0.01 | 30.04 | PASS |
| 2 | v0.2 default ABC | 25 | 0 | 25/5 | 25 | 0.03 | 0.01 | 30.86 | PASS |
| 2 | v0.2 custom ABC | 20 | 0 | 25/5 | 20 | 0.03 | 0.01 | 30.93 | PASS |
| 3 | baseline | 13 | 17 | 3/9 | 30 | 0.03 | 0.01 | 30.95 | PASS |
| 3 | v0.2 default ABC | 11 | 12 | 3/9 | 23 | 1.27 | 0.01 | 31.65 | PASS |
| 3 | v0.2 custom ABC | 11 | 12 | 3/9 | 23 | 1.25 | 0.01 | 31.69 | PASS |
| 4 | baseline | 36 | 10 | 8/10 | 46 | 0.04 | 0.01 | 31.02 | PASS |
| 4 | v0.2 default ABC | 27 | 10 | 8/10 | 37 | 0.10 | 0.01 | 31.68 | PASS |
| 4 | v0.2 custom ABC | 27 | 10 | 8/10 | 37 | 0.10 | 0.01 | 31.71 | PASS |

The C selection rule is the same for every design: a candidate must have a nonempty structural plan, whole-design EQY PASS, no LUT/DFF regression, identical IO, then minimize LUT, DFF, total cells and variant name. It selects baseline, baseline, custom, custom for cases 1–4, respectively. Case2's 20-LUT custom ABC result is an observed mapping-only effect with `structural_edits=[]`; it is reported, but is not claimed as a v0.2 structural improvement or selected by this rule. The selected official result is **288/222, 25/0, 11/12, 27/10**, all EQY PASS. The custom script tie choice for cases 3/4 has no resource advantage over default ABC.

## Plan, proof and actual netlist audit

- Cases 1/2: `decision_cone_candidates=0`, `structural_edits=[]`, `no_v02_case34_candidate` present in explicit candidate stats; no resource regression.
- Case3: `decision_cone_candidates=1`, `committed_candidates=1`; sole rule `guarded_next_state_decision_cone`, `proof_reason=proved`, plan `added_cells=1`, `removed_cells=0`. C before/after insertion JSON audit finds exactly +1 `$or` and three comparator A/B widths 8→7, each in the proof's source comparator set. Four sequential D connections change; clock/reset/enable/Q/init and top ports are semantically identical, using named-net bit canonicalization to account for JSON bit-ID renumbering. Final 11 `$lut`, 12 `dffeas`.
- Case4: `decision_cone_candidates=0`, one `constant_table_cofactor` plan, source mux removal 16 and added cells 26. C insertion diff confirms those counts, one shared mux retained with width 2→1, no sequential control/init or port change; final 27 `$lut`, 10 `dffeas`. The plan's split vector is evidence of the proof outcome, not a runtime dispatch key.
- Production matcher and generation code were audited for case-number/file/module/signal/automatic-cell-name, fixed comparator constant, fixed truth-table and split-vector dispatch. The runtime match and proof use cell type, connectivity, support and proved facts. C's `test1`–`test4` loop is only an official evaluation harness; the candidate selector itself has no case-specific branch. Corrected exact/signal identity scans have zero hits for forbidden runtime dispatch patterns. The `-v02-case34` switch remains explicitly required for these production candidates.

## Verdict and boundary

- **P0_PASS:** all 4 selected official gates, and all 12 matrix gates, whole-design EQY PASS.
- **P1_PASS:** no identity/fixed-vector runtime dispatch found in production proof or rewrite path.
- **P2_PASS:** selected four cases meet the stated oracle without resource or IO regression.
- **PRODUCTION_CANDIDATES_ACCEPTED** for the explicit switch.
- **G3 = BLOCKED_BY_UNIFIED_SELECTION_GATE.** A unified cost/EQY automatic choice is not yet in the production default pipeline. C's test harness selector does not constitute that integration.

## Evidence and reproduction

Tracked compressed archives and digest index: `c_final_evidence/`; see its `MANIFEST.md`. Full expanded evidence is on this host at `/home/yuewen/work/pmux-yosys-c-v02-final/pmux_project/reports/c-v02-final-acceptance/`. The archives contain the full 12-gate matrix including EQY work directories, both 1000-seed runs, all 19 micro tests, insertion before/after nets, proof reports and atomic rollback. `official_audit.json`, `insertion_audit.json` and `random_audit.json` are C's independent assertions. The random archives include the full per-seed logs; the frozen seed inventory is `c_final_evidence/seeds_1000.txt`. Their summaries alone are not the basis for acceptance. The official matrix command is implemented in `pmux_project/scripts/c_v02_official_matrix.py`; the explicit random adapter is `pmux_project/scripts/c_v02_case34_adapter.py`. No production source was changed by C.
