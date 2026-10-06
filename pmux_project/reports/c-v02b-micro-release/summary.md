# V2-B micro fixture regression

Fixture PASS 22/22; FAIL 0. 真实 Pass、生产 LUT 收益、1000 例真实阶段验收均为 NOT_RUN。

| 用例 | 原因类别 | 结构 | Check | EQY | ABC9前组合cell | LUT前→后 | DFF前→后 | IO前→后 | CPU s | 峰值MB |
|---|---|---|---|---|---:|---:|---:|---:|---:|---:|
| 01_first | fixture_branch_delete | PASS | PASS | PASS | 13 | 4→4 | 0→0 | 0→0 | 0.02 | 26.26 |
| 02_middle | fixture_branch_delete | PASS | PASS | PASS | 12 | 2→2 | 0→0 | 0→0 | 0.02 | 26.39 |
| 03_last | fixture_branch_delete | PASS | PASS | PASS | 12 | 2→2 | 0→0 | 0→0 | 0.03 | 26.48 |
| 04_more_than_two_left | fixture_branch_delete | PASS | PASS | PASS | 19 | 4→4 | 0→0 | 0→0 | 0.02 | 25.62 |
| 05_to_mux | fixture_branch_delete | PASS | PASS | PASS | 4 | 2→2 | 0→0 | 0→0 | 0.02 | 26.43 |
| 06_last_to_a | fixture_branch_delete | PASS | PASS | PASS | 0 | 0→0 | 0→0 | 0→0 | 0.02 | 26.05 |
| 07_multi_delete | fixture_branch_delete | PASS | PASS | PASS | 13 | 2→2 | 0→0 | 0→0 | 0.03 | 26.62 |
| 08_equal_default_a | fixture_branch_delete | PASS | PASS | PASS | 12 | 2→2 | 0→0 | 0→0 | 0.02 | 26.23 |
| 09_equal_lower_fallback | fixture_fallback_equal | PASS | PASS | PASS | 2 | 1→1 | 0→0 | 0→0 | 0.02 | 26.18 |
| 10_exclusive_select | fixture_branch_delete | PASS | PASS | PASS | 13 | 4→4 | 0→0 | 0→0 | 0.02 | 26.62 |
| 11_possible_overlap | fixture_select_overlap | PASS | PASS | PASS | 1 | 1→1 | 0→0 | 0→0 | 0.03 | 26.43 |
| 12_partial_bits_reject | fixture_partial_bits | PASS | PASS | PASS | 9 | 3→3 | 0→0 | 0→0 | 0.03 | 26.48 |
| 13_proof_unknown | fixture_proof_unknown | PASS | PASS | PASS | 6 | 2→2 | 0→0 | 0→0 | 0.02 | 26.2 |
| 14_condition_conflict | fixture_condition_conflict | PASS | PASS | PASS | 1 | 1→1 | 0→0 | 0→0 | 0.02 | 26.45 |
| 15a_depth_budget | fixture_budget_depth | PASS | PASS | PASS | 5 | 2→2 | 0→0 | 0→0 | 0.02 | 25.78 |
| 15b_cell_budget | fixture_budget_cells | PASS | PASS | PASS | 5 | 2→2 | 0→0 | 0→0 | 0.02 | 25.68 |
| 15c_bit_budget | fixture_budget_bits | PASS | PASS | PASS | 5 | 2→2 | 0→0 | 0→0 | 0.02 | 26.18 |
| 15d_event_budget | fixture_budget_events | PASS | PASS | PASS | 5 | 2→2 | 0→0 | 0→0 | 0.02 | 26.07 |
| 16_keep | fixture_keep | PASS | PASS | PASS | 4 | 2→2 | 0→0 | 0→0 | 0.02 | 26.38 |
| 17_selection_partial | fixture_selection_scope | PASS | PASS | PASS | 13 | 4→4 | 0→0 | 0→0 | 0.03 | 26.04 |
| 18_snapshot_mismatch | fixture_snapshot_mismatch | PASS | PASS | PASS | 4 | 2→2 | 0→0 | 0→0 | 0.02 | 25.95 |
| 19_invalid_branch | fixture_invalid_branch_index | PASS | PASS | PASS | 4 | 2→2 | 0→0 | 0→0 | 0.02 | 26.35 |

所有改写和拒绝均为预制 fixture；预算耗尽为强制注入。本表不判定生产 LUT 收益。
