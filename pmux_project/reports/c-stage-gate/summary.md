# C 阶段验收

固定种子：1000/1000；结构、综合、EQY：PASS/PASS/PASS。
官方基线重现：PASS；优化结构：PASS；EQY：PASS；LUT 收益：FAIL。
生产版加载验证：RUN_VERIFIED；允许合入 integration/g3：NO。

| Case | LUT | DFF | IO | CPU s | Peak MB | 改写位 | EQY 路径 | ABC 内部报错 |
|---|---:|---:|---:|---:|---:|---:|---|---|
| case1 | 288→288 | 222→222 | 0→0 | 0.09→0.12 | 31.27→33.94 | 0 | whole_design | True |
| case2 | 25→25 | 0→0 | 0→0 | 0.03→0.03 | 26.3→28.73 | 0 | default_partitions | True |
| case3 | 13→13 | 17→17 | 0→0 | 0.03→0.03 | 26.91→29.96 | 7 | whole_design | True |
| case4 | 36→36 | 10→10 | 0→0 | 0.04→0.04 | 27.4→29.39 | 0 | default_partitions | True |

阻塞：official_lut_benefit_failed

官方优化逐阶段调用 B 的审计入口，使用 `-m` 加载真实插件；完整综合及后 ABC9 各执行一次 PMUX_OPT，并核验 MEMORY_MAP < PMUX_OPT < OPT_CLEAN < TECHMAP < ABC9。
