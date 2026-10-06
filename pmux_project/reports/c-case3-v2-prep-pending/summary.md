# case3 V2 实验矩阵

模式：REAL_VARIANTS；官方输入 SHA-256：1551b9212622ccef6019d6e77de2c43dc8c80d267957b55a42121f34a08780e9。
HARNESS_SMOKE 只验证运行器，不代表 B 变体或真实 LUT 收益。

| 变体 | Check | EQY | ABC9前组合cell | LUT | ΔLUT | DFF/IO | 判定 |
|---|---|---|---:|---:|---:|---|---|
| baseline | PASS | PASS | 54 | 13 | — | 17/0 | REFERENCE |
| V2-A | NOT_RUN | NOT_RUN | — | — | — | — | NOT_RUN |
| V2-B | NOT_RUN | NOT_RUN | — | — | — | — | NOT_RUN |
| V2-C | NOT_RUN | NOT_RUN | — | — | — | — | NOT_RUN |
| V2-A+B | NOT_RUN | NOT_RUN | — | — | — | — | NOT_RUN |
| V2-A+B+C | NOT_RUN | NOT_RUN | — | — | — | — | NOT_RUN |

ABC9 前 AND/OR/NOT/MUX 及各阶段完整 cell 统计见 summary.json；所有 RTLIL、JSON、脚本、命令和日志见 evidence.tar.gz。

| 变体 | common techmap后组合cell | ABC9前AND | OR | NOT | MUX |
|---|---:|---:|---:|---:|---:|
| baseline | 69 | 7 | 19 | 20 | 8 |
| V2-A | — | — | — | — | — |
| V2-B | — | — | — | — | — |
| V2-C | — | — | — | — | — |
| V2-A+B | — | — | — | — | — |
| V2-A+B+C | — | — | — | — | — |

B 的五个真实变体尚未完成本矩阵；收益结论为 NOT_RUN。
