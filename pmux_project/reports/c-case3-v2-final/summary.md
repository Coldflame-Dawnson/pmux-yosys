# case3 V2 实验矩阵

模式：REAL_VARIANTS；官方输入 SHA-256：1551b9212622ccef6019d6e77de2c43dc8c80d267957b55a42121f34a08780e9。
HARNESS_SMOKE 只验证运行器，不代表 B 变体或真实 LUT 收益。

真实候选为 B 的冻结 case3 结构消融输出；本矩阵不将规则接入默认 Pass。Baseline 的 ABC9 顶层 JSON 与既有官方基线完全相同。

| 变体 | Check | EQY | ABC9前组合cell | LUT | ΔLUT | DFF/IO | 判定 |
|---|---|---|---:|---:|---:|---|---|
| baseline | PASS | PASS | 54 | 13 | — | 17/0 | REFERENCE |
| V2-A | PASS | PASS | 51 | 13 | 0 | 17/0 | NO_GAIN |
| V2-B | PASS | PASS | 52 | 12 | -1 | 17/0 | GAIN |
| V2-C | PASS | PASS | 52 | 13 | 0 | 17/0 | NO_GAIN |
| V2-A+B | PASS | PASS | 49 | 12 | -1 | 17/0 | GAIN |
| V2-A+B+C | PASS | PASS | 47 | 12 | -1 | 17/0 | GAIN |

ABC9 前 AND/OR/NOT/MUX 及各阶段完整 cell 统计见 summary.json；所有 RTLIL、JSON、脚本、命令和日志见 evidence.tar.gz。

| 变体 | common techmap后组合cell | ABC9前AND | OR | NOT | MUX |
|---|---:|---:|---:|---:|---:|
| baseline | 69 | 7 | 19 | 20 | 8 |
| V2-A | 66 | 7 | 19 | 20 | 5 |
| V2-B | 65 | 6 | 18 | 20 | 8 |
| V2-C | 66 | 7 | 19 | 20 | 6 |
| V2-A+B | 62 | 6 | 18 | 20 | 5 |
| V2-A+B+C | 59 | 6 | 18 | 20 | 3 |

产生收益的最小变体：V2-B。
