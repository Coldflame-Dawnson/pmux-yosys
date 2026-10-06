# 官方 case 阶段差异诊断

每个截点独立运行并在该点停止，避免网表导出影响后续 ABC9 映射。optimized 的插入点前截点尚未执行 Pass；其余截点每进程恰好执行一次真实 Pass。baseline 全程不加载插件。ABC9 后直接使用 B 原始审计入口，资源数与既有官方回归一致，并逐案通过原始 RTL 对阶段5网表的 EQY。

| Case | 阶段 | ΔPMUX | ΔMUX | ΔEQ | ΔNE | ΔAND | ΔOR | ΔNOT | ΔXOR | Δ基本MUX | 总组合 baseline→optimized (Δ) | LUT baseline→optimized (Δ) | 结构同 | 统计同 |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|
| case1 | 01_pre_pmux | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 22→22 (+0) | 0→0 (+0) | True | True |
| case1 | 02_post_pass_opt_clean | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 22→22 (+0) | 0→0 (+0) | True | True |
| case1 | 03_post_common_techmap | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1015→1015 (+0) | 0→0 (+0) | True | True |
| case1 | 04_pre_abc9 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 843→843 (+0) | 0→0 (+0) | True | True |
| case1 | 05_post_abc9 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 288→288 (+0) | 288→288 (+0) | True | True |
| case2 | 01_pre_pmux | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4→4 (+0) | 0→0 (+0) | True | True |
| case2 | 02_post_pass_opt_clean | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4→4 (+0) | 0→0 (+0) | True | True |
| case2 | 03_post_common_techmap | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 20→20 (+0) | 0→0 (+0) | True | True |
| case2 | 04_pre_abc9 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 20→20 (+0) | 0→0 (+0) | True | True |
| case2 | 05_post_abc9 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 25→25 (+0) | 25→25 (+0) | True | True |
| case3 | 01_pre_pmux | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 12→12 (+0) | 0→0 (+0) | True | True |
| case3 | 02_post_pass_opt_clean | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 12→12 (+0) | 0→0 (+0) | False | True |
| case3 | 03_post_common_techmap | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 69→69 (+0) | 0→0 (+0) | False | True |
| case3 | 04_pre_abc9 | 0 | 0 | 0 | 0 | -1 | -1 | 0 | 0 | 0 | 54→52 (-2) | 0→0 (+0) | False | False |
| case3 | 05_post_abc9 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 13→13 (+0) | 13→13 (+0) | False | True |
| case4 | 01_pre_pmux | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 23→23 (+0) | 0→0 (+0) | True | True |
| case4 | 02_post_pass_opt_clean | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 23→23 (+0) | 0→0 (+0) | True | True |
| case4 | 03_post_common_techmap | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 118→118 (+0) | 0→0 (+0) | True | True |
| case4 | 04_pre_abc9 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 73→73 (+0) | 0→0 (+0) | True | True |
| case4 | 05_post_abc9 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 36→36 (+0) | 36→36 (+0) | True | True |

总组合 cell 的下降只作结构观察；LUT 收益只按 ABC9 后 `$lut` 判定。

## case3 七位链

opt_clean 删除 0 个 cell；首次结构重新相同：未出现；首次请求统计重新相同：05_post_abc9。

结论：七位替换未使 opt_clean 删除 cell；ABC9 前少 2 个基本组合 cell，ABC9 后均为 13 个 LUT，映射吸收了门级差异。

| Cell | 端口位 | Branch | 原信号 | 常量 | 原扇出 cell |
|---|---|---|---|---|---|
| $procmux$10 | B[0] | None | sr[2] | 1 | $auto$ff.cc:337:slice$44, $procmux$10, $procmux$20_CMP0, $procmux$27, $procmux$28_CMP0, $procmux$29_CMP0 |
| $procmux$10 | B[1] | None | sr[1] | 1 | $auto$ff.cc:337:slice$44, $procmux$10, $procmux$20_CMP0, $procmux$27, $procmux$28_CMP0, $procmux$29_CMP0 |
| $procmux$10 | B[2] | None | sr[0] | 1 | $auto$ff.cc:337:slice$44, $procmux$10, $procmux$18, $procmux$20_CMP0, $procmux$27, $procmux$28_CMP0, $procmux$29_CMP0 |
| $procmux$18 | B[0] | 0 | sr[3] | 1 | $auto$ff.cc:337:slice$44, $auto$ff.cc:337:slice$62, $procmux$18, $procmux$20_CMP0, $procmux$27, $procmux$28_CMP0, $procmux$29_CMP0 |
| $procmux$27 | B[1] | None | sr[6] | 1 | $auto$ff.cc:337:slice$44, $procmux$10, $procmux$20_CMP0, $procmux$27, $procmux$28_CMP0, $procmux$29_CMP0 |
| $procmux$27 | B[2] | None | sr[5] | 1 | $auto$ff.cc:337:slice$44, $procmux$10, $procmux$20_CMP0, $procmux$27, $procmux$28_CMP0, $procmux$29_CMP0 |
| $procmux$27 | B[3] | None | sr[4] | 1 | $auto$ff.cc:337:slice$44, $procmux$10, $procmux$20_CMP0, $procmux$27, $procmux$28_CMP0, $procmux$29_CMP0 |

完整扇出端口、驱动、opt_clean 删除清单和各位删除关系见 `case3/seven_bit_chain.json`。
