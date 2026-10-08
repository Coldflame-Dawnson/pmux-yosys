# Role C final independent acceptance of B `99dee059`

Date: 2026-10-09. Verdict: **P0_PASS / P1_PASS / P2_PASS / PRODUCTION_CANDIDATES_ACCEPTED**. G3: **BLOCKED_BY_UNIFIED_SELECTION_GATE**.

This is a fresh C run. No B PASS file or prior C PASS file was used as a conclusion. C changed only test drivers, this report and evidence; the proof engine, production Pass, frozen API, official RTL, scoring expectations, `synth_intel` and ABC9 production scripts were untouched.

## Authorization and provenance

- A report `pmux_project/analyzer/decision_cone_consumer_ordered_reaudit_99dee05.md` on A HEAD `6ff4bd10ddd8995fc5d7f3a65d7150c2a3f6da53` explicitly says `AUTHORIZED` for B `99dee059077e83969cede6e292dc1ea9139389e3`. Its audit-content commit is `3b9185c86e23b365445b6a31f5d914e0da7f293f`.
- New C branch `feature/c-v02-case34-final-acceptance-99dee05` started with clean HEAD exactly `99dee059077e83969cede6e292dc1ea9139389e3`. Origin is `https://github.com/Coldflame-Dawnson/pmux-yosys.git`. Parent chain: `25217ab0b` → `44bafd2df` → `99dee0590`; A's final audit commit descends from that B commit.
- B's `25217ab0b..99dee0590` diff contains only `pmux_project/pass/case34_rewrite.cc`, `.h`, `pmux_opt.cc` and two production status documents. It contains no official input, frozen interface, expectation, toolchain or production synthesis script change. C's commit diff is restricted to C scripts/report/evidence.
- Official input SHA-256: test1 `2d2dba011b0e8799d0de3790b69b7f01c6eb6ac665c4321b57b0378a4972fd13`; test2 `e43c9b437071488551caf3121e227e86f4fe3190751f245beac3b7ecc3fdb146`; test3 `1551b9212622ccef6019d6e77de2c43dc8c80d267957b55a42121f34a08780e9`; test4 `c0c5042928bd6bf568e2b7fb2193f2b6db9c4193ba3a5378fc50a331d43422d7`.
- Frozen source commits match exactly: Yosys `9f75ca1f9834a39a863915b5dae0c7b1e33533bc`, EQY `4a72eb94fc253062464afee4d0018359017bb846`, SBY `b1a1e98cba941ec8433f8dc27f416cd7bb7f14be`, ABC `a51bf4a7bb38ce06ea3a544b1463b40832213917`. Raw version output and executable SHA-256 are in `c_99dee05_evidence/toolchain.txt` and `toolchain.json`. Rebuilt C analyzer plugin SHA-256 `fd09099374a4095fe0df36defe1d13a2ea2c653764ab8ffeae22e32f3606508e`; rebuilt Pass SHA-256 `0b8c97c6ac8c7670ed3e562a294f044e7017ae9a896756ad38e4361f0cf92c26`.

## Hard tests

1. **Mutation seam 26/26 PASS.** C ran `read_verilog test3.v; synth_intel -family cycloneiv -top test3 -run begin:map_ffram; opt -fast -mux_undef -undriven -fine -full; memory_map; opt -undriven -fine; pmux_opt -test-decision-proof-mutations` using its freshly built plugin. The 26 checks report `failures=0`; every reject path in the seam compares a complete `RTLIL_BACKEND::dump_module` fingerprint before/after the validation call. A full before/after JSON export is byte-identical; Yosys `check` finds zero problems. Text RTLIL exports reorder objects, so their file SHA-256 values differ despite identical JSON and internal rejection fingerprints. Full script, exports and log are archived.
2. **Analyzer selftests PASS.** Fast counts 2116/29/60/50/81; slow counts 2118/29/60/50/81. All case3/case4, production and decision-cone read-only reports pass. Their netlist-unchanged markers are 1. The production case4 report has 32 distinct addresses 0–31, each 10 bits; C independently parses the official Verilog case arms and verifies all 32 values. Address 0 is `1001110000`.
3. **Real micro and rollback PASS.** 19/19 micro tests have structure, `check`, EQY and semantic PASS. The test-only snapshot abort has exact zero structural write, `check` PASS and EQY PASS.
4. **Fixed seeds PASS.** From frozen `seeds_1000.txt` (SHA-256 `8db91b2ee25d579493dbc2ca66417cc945e215b5424349884013834d43df7ac4`), default Pass mode ran first: 1000/1000 real synthesis, structure and EQY PASS. C's own adapter then ran explicit `pmux_opt -v02-case34`: 1000/1000 PASS. The explicit run had zero structural-match seeds; every plan has `structural_edits=[]`, `changed_structures=0` and `no_v02_case34_candidate`. No failure seed or misproof occurred; `first_failure.txt` says `NONE`. Full per-seed before/after nets, plans, stats, commands, logs and EQY work directories are archived.
5. **Official gates PASS.** C generated 12 isolated gates: baseline, explicit v0.2/default ABC and explicit v0.2/custom ABC for all four official cases. Every generated gate passed `check` and separate official RTL whole-design EQY, with `.eqy`, full log and work `PASS` preserved. C's selector uses only nonempty structural plan, EQY PASS, resource/IO guards and a generic resource tie break. It selects baseline for case1/2 and custom for case3/4. The case2 custom ABC map has 20 LUT but no v0.2 structural edit, so it is recorded as a mapping-only observation and not selected as a production case34 candidate.

## Post-ABC9 resource and runtime matrix

`$lut` and `dffeas` are top-module post-ABC9 JSON counts; IO is **input/output port bits**, with inout 0 in every row. Thus the requested IO regression is zero. Total cells are post-ABC9 cell counts, not vendor primitives. CPU is Yosys user+system seconds, ABC is its reported elapsed seconds, and memory is Yosys peak MB. Every row has `check=PASS`, official-gold whole-design `EQY=PASS`.

| case | gate | `$lut` | DFF | IO in/out | cells | CPU s | ABC s | peak MB | EQY |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| 1 | baseline | 288 | 222 | 259/26 | 510 | 0.11 | 0.03 | 36.22 | PASS |
| 1 | v0.2 default ABC | 288 | 222 | 259/26 | 510 | 0.11 | 0.03 | 36.30 | PASS |
| 1 | v0.2 custom ABC | 288 | 222 | 259/26 | 510 | 0.11 | 0.09 | 37.01 | PASS |
| 2 | baseline | 25 | 0 | 25/5 | 25 | 0.03 | 0.01 | 31.78 | PASS |
| 2 | v0.2 default ABC | 25 | 0 | 25/5 | 25 | 0.02 | 0.01 | 31.33 | PASS |
| 2 | v0.2 custom ABC | 20 | 0 | 25/5 | 20 | 0.03 | 0.01 | 31.62 | PASS |
| 3 | baseline | 13 | 17 | 3/9 | 30 | 0.03 | 0.01 | 32.18 | PASS |
| 3 | v0.2 default ABC | 11 | 12 | 3/9 | 23 | 1.16 | 0.01 | 32.25 | PASS |
| 3 | v0.2 custom ABC | 11 | 12 | 3/9 | 23 | 1.17 | 0.01 | 32.27 | PASS |
| 4 | baseline | 36 | 10 | 8/10 | 46 | 0.04 | 0.01 | 31.78 | PASS |
| 4 | v0.2 default ABC | 27 | 10 | 8/10 | 37 | 0.09 | 0.01 | 32.18 | PASS |
| 4 | v0.2 custom ABC | 27 | 10 | 8/10 | 37 | 0.10 | 0.01 | 32.82 | PASS |

Selected official result: **case1 288/222, case2 25/0, case3 11/12, case4 27/10**, all without IO growth and all whole-design EQY PASS. Default and custom ABC have equal candidate resources for case3/4; the custom selection is a generic tie break, not a case-specific branch. Individual gate SHA-256 values, gold hashes, scripts, JSON/RTLIL, plans/stats and runtime logs are in the official matrix archive and `summary.json`.

## Structural and identity audit

- Case1/2 explicit v0.2 plans have `structural_edits=[]`, `decision_cone_candidates=0`, `committed_candidates=0`, reason `no_v02_case34_candidate` and unchanged 288/222 and 25/0 default-map resources.
- Case3 has exactly one committed `guarded_next_state_decision_cone` candidate. The insertion diff adds one combinational `$or`, removes no cell and narrows three proof-authorized `$eq` comparators from 8/8 to 7/7 input widths. Four sequential D connections change. The audit canonicalizes net bits by named nets and confirms B did not directly alter clock, reset, enable, Q, init or module ports. Final 11 `$lut`, 12 `dffeas`.
- Case4 has exactly one committed `constant_table_cofactor` candidate and zero decision-cone candidates. The insertion diff deletes 16 source muxes, retains and narrows one shared mux 2→1, and adds 26 cells. Clock/reset/enable/Q/init/ports are semantically unchanged. The full 5×10, 32-row official table is reconstructed and matched as described above. Final 27 `$lut`, 10 `dffeas`.
- C scanned production proof/consumer and synthesis runner for case-number, test/module/file/automatic-cell literal, fixed row and fixed split dispatch. No forbidden literal hit was found. All 37 dynamic `module->name`, `cell->name` and `anchor_cell` sites were reviewed as proof-object identity binding, claimed-cell conflict tracking or plan/report output; none enables a rule based on a particular identity value. `-v02-case34` remains the explicit gate.

## B digest comparison and limits

B supplied case3 `62b9aeb5…` and case4 `b6a2d90c…` as **reference digests only**. C's selected gate RTLIL hashes are case3 `7d5d34cdc66cf70900ec8743a3eaa56c0400ab6e499854f6d788238ed13e2db5` and case4 `009f8ad4ebaf437099090f5f0faa24815c07faf2eb65bacc69a9bd02be100de3`. The referenced B gate bytes are in B's ignored local report directory, absent from the reviewed commit; therefore an exact line-by-line diff against those two particular hashes is unavailable. C's source paths and Yosys build banner differ from B's documented local run, and `NEW_ID` includes source-line-dependent automatic names; these are known causes of byte variance, not a claim explaining every unseen B line. The hard gates are independently observed resources, structure and official-gold whole-design EQY. The C gate bytes and complete commands are provided for direct review.

## Final decision

- **P0_PASS**: all 12 newly generated gates, including the four selected official gates, pass whole-design EQY; no mismatch or failing seed.
- **P1_PASS**: mutation seam 26/26, fingerprint rejection checks, atomic rollback, and identity-dispatch audit pass.
- **P2_PASS**: selected resources exactly meet the 288/222, 25/0, 11/12 and 27/10 hard targets with IO delta 0.
- **PRODUCTION_CANDIDATES_ACCEPTED** for explicit `-v02-case34` production use.
- **G3 = BLOCKED_BY_UNIFIED_SELECTION_GATE**. Unified default choice, cost gate and EQY-failure fallback are not in the production default path; C did not enable them.

The tracked evidence directory `c_99dee05_evidence/` has `MANIFEST.md`, `SHA256SUMS`, `toolchain.txt`, `summary.json`, full compressed logs/nets/EQY work and C audit scripts. Expanded local evidence: `/home/yuewen/work/pmux-yosys-c-99dee05/pmux_project/reports/c-99dee05/`.
