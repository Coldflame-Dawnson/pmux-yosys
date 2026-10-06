# case3 V2 实验矩阵

模式：HARNESS_SMOKE；官方输入 SHA-256：1551b9212622ccef6019d6e77de2c43dc8c80d267957b55a42121f34a08780e9。
HARNESS_SMOKE 只验证运行器，不代表 B 变体或真实 LUT 收益。

| 变体 | Check | EQY | ABC9前组合cell | LUT | ΔLUT | DFF/IO | 判定 |
|---|---|---|---:|---:|---:|---|---|
| baseline | PASS | PASS | 54 | 13 | — | 17/0 | REFERENCE |
| V2-A | PASS | PASS | 54 | 13 | 0 | 17/0 | HARNESS_ONLY |
| V2-B | PASS | PASS | 54 | 13 | 0 | 17/0 | HARNESS_ONLY |
| V2-C | PASS | PASS | 54 | 13 | 0 | 17/0 | HARNESS_ONLY |
| V2-A+B | PASS | PASS | 54 | 13 | 0 | 17/0 | HARNESS_ONLY |
| V2-A+B+C | PASS | PASS | 54 | 13 | 0 | 17/0 | HARNESS_ONLY |

ABC9 前 AND/OR/NOT/MUX 及各阶段完整 cell 统计见 summary.json；所有 RTLIL、JSON、脚本、命令和日志见 evidence.tar.gz。

| 变体 | common techmap后组合cell | ABC9前AND | OR | NOT | MUX |
|---|---:|---:|---:|---:|---:|
| baseline | 69 | 7 | 19 | 20 | 8 |
| V2-A | 69 | 7 | 19 | 20 | 8 |
| V2-B | 69 | 7 | 19 | 20 | 8 |
| V2-C | 69 | 7 | 19 | 20 | 8 |
| V2-A+B | 69 | 7 | 19 | 20 | 8 |
| V2-A+B+C | 69 | 7 | 19 | 20 | 8 |

B 的五个真实变体尚未完成本矩阵；收益结论为 NOT_RUN。
