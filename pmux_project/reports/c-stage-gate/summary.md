# C 阶段验收

固定种子：1000/1000；结构、综合、EQY：PASS/PASS/PASS。
官方基线重现：PASS；优化结构：PASS；EQY：PASS；LUT 收益：FAIL。
生产版加载验证：PENDING_B_CONFIRMATION；允许合入 integration/g3：NO。

| Case | LUT | DFF | IO | CPU s | Peak MB | 改写位 | EQY 路径 | ABC 内部报错 |
|---|---:|---:|---:|---:|---:|---:|---|---|
| case1 | 288→288 | 222→222 | 0→0 | 0.1→0.11 | 31.15→34.1 | 0 | whole_design | True |
| case2 | 25→25 | 0→0 | 0→0 | 0.03→0.02 | 26.97→29.0 | 0 | default_partitions | True |
| case3 | 13→13 | 17→17 | 0→0 | 0.03→0.03 | 27.36→29.04 | 7 | whole_design | True |
| case4 | 36→36 | 10→10 | 0→0 | 0.04→0.05 | 27.39→29.9 | 0 | default_partitions | True |

阻塞：official_lut_benefit_failed, production_yosys_loading_pending_B

分段综合按冻结的 map_ffram 顺序调用真实插件；完整综合及后 ABC9 各执行一次 PMUX_OPT。生产版加载方式由 B 确认后用 `--real-flow integrated` 重跑。
