# C 真实 Pass 联调

分支：`integration/c-real-m1`。A/B/C 冻结输入：`445231c4c616781a957292699376cb73bca60037` / `69caa80a8d610ceaca4ddba92721c2a294c5faa0` / `71bfcfc97e5a8d42635586ba96d461f0107f9518`。

19 微型：19/19；结构 PASS；EQY PASS。
100 随机：100/100；综合 PASS；结构 PASS；EQY PASS；7 例共 24 位改写。
原子回退测试专用模式：PASS；LUT 收益：NOT_RUN；进入 1000 例：YES。

| 用例 | 结果 | 改写位 | 结构 | check | EQY | 原因码 |
|---|---|---:|---|---|---|---|
| 01_mux_a | PASS | 1 | PASS | PASS | PASS | primary_input |
| 02_mux_b | PASS | 1 | PASS | PASS | PASS | primary_input |
| 03_pmux_a | PASS | 1 | PASS | PASS | PASS | primary_input |
| 04_pmux_bi | PASS | 1 | PASS | PASS | PASS | primary_input |
| 05_eq_reverse | PASS | 4 | PASS | PASS | PASS | primary_input |
| 06_ne_zero_reverse | PASS | 4 | PASS | PASS | PASS | primary_input |
| 07_partial_bit | PASS | 1 | PASS | PASS | PASS | primary_input |
| 08_shared_source | PASS | 1 | PASS | PASS | PASS | primary_input |
| 09_overlap_pmux | PASS | 2 | PASS | PASS | PASS | primary_input |
| 10_xz_untouched | PASS | 1 | PASS | PASS | PASS | original_xz, primary_input |
| 11_ambiguous_compare | PASS | 0 | PASS | PASS | PASS | primary_input |
| 12_unsupported_cell | PASS | 0 | PASS | PASS | PASS | primary_input, unsupported_cell |
| 13_fact_conflict | PASS | 0 | PASS | PASS | PASS | fact_conflict, primary_input |
| 14a_budget_cells | PASS | 0 | PASS | PASS | PASS | budget_cells_exhausted, budget_exhausted |
| 14b_budget_bits | PASS | 0 | PASS | PASS | PASS | budget_bits_exhausted, budget_exhausted |
| 14c_budget_events | PASS | 512 | PASS | PASS | PASS | budget_events_exhausted, budget_exhausted |
| 15_multi_branch_merge | PASS | 2 | PASS | PASS | PASS | primary_input |
| 16_no_opportunity | PASS | 0 | PASS | PASS | PASS | primary_input |
| 17_case3_seven_bits | PASS | 7 | PASS | PASS | PASS | primary_input |

真实运行的每例目录保留 before/after、Pass 原生 plan/stats、命令、Yosys 与 EQY 完整日志。原子回退预检使用 B 测试专用开关，未计入真实证明。

最终 19+100 回归无失败用例；早期 14c 测试数据校准失败的 before/after/plan/stats/EQY 日志归档为 `reports/c-real-integration/initial-14c-fixture-calibration.tar.gz`。该次错误来自 C 夹具未达到事件预算，已修正并在最终 14c 通过。
