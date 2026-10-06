# PMUX v0.2-B C 验证门槛

ready_to_merge_integration_g3=false。
A/B 真实生产提交尚未交付；fixture 及冻结 case3 消融不作为生产规则收益。

定向 fixture：PASS (22/22)；随机 100 fixture：PASS (100/100)；随机 1000 真实验收：NOT_RUN。

| case | baseline LUT | optimized LUT | baseline DFF/IO | optimized DFF/IO | baseline CPU s/峰值MB | optimized CPU s/峰值MB | EQY | Pass 审计 | 已知 ABC 报错 |
|---|---:|---:|---|---|---|---|---|---|---|
| case1 | 288 | NOT_RUN | 222/0 | NOT_RUN | 0.11/31.54 | NOT_RUN | PASS→NOT_RUN | NOT_RUN | True |
| case2 | 25 | NOT_RUN | 0/0 | NOT_RUN | 0.03/26.81 | NOT_RUN | PASS→NOT_RUN | NOT_RUN | True |
| case3 | 13 | NOT_RUN | 17/0 | NOT_RUN | 0.03/26.98 | NOT_RUN | PASS→NOT_RUN | NOT_RUN | True |
| case4 | 36 | NOT_RUN | 10/0 | NOT_RUN | 0.04/27.14 | NOT_RUN | PASS→NOT_RUN | NOT_RUN | True |

冻结 case3 结构消融 V2-B 再次通过 EQY，13→12 LUT；这不计入上表 optimized 生产列。

| 阶段门槛 | 状态 |
|---|---|
| directed_structure_and_eqy | NOT_MET |
| random_100_zero_failures | NOT_MET |
| random_1000_zero_failures | NOT_MET |
| snapshot_mismatch_zero_write | NOT_MET |
| case3_one_lut_gain | NOT_MET |
| other_official_no_regression | NOT_MET |
| machine_plan_stats_reason_complete | NOT_MET |
| real_a_plus_b_path_and_order | NOT_MET |

失败种子与完整命令/网表/日志位于相应 suite 的 evidence.tar.gz。
