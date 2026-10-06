# 官方四例回归 (identity)

结构改写：NOT_RUN；EQY：PASS；LUT 收益：NOT_RUN。

真实模式逐阶段调用 B 的审计式 run_synth_intel_pmux.py，使用 -m 加载真实插件；CPU 与峰值内存均取完整综合 Yosys 日志。

| Case | LUT 基线→优化 | DFF 基线→优化 | IO 基线→优化 | CPU s 基线→优化 | Peak MB 基线→优化 | 改写位 | Structure | PMUX position | EQY | Method | ABC error |
|---|---:|---:|---:|---:|---:|---:|---|---|---|---|---|
| case1 | 288→288 | 222→222 | 0→0 | 0.14 s→0.09 | 27.84 MB→31.27 |  | NOT_RUN | NOT_RUN | PASS | whole_design | True |
| case2 | 25→25 | 0→0 | 0→0 | 0.08 s→0.03 | 22.27 MB→26.3 |  | NOT_RUN | NOT_RUN | PASS | default_partitions | True |
| case3 | 13→13 | 17→17 | 0→0 | 0.07 s→0.03 | 22.65 MB→26.91 |  | NOT_RUN | NOT_RUN | PASS | whole_design | True |
| case4 | 36→36 | 10→10 | 0→0 | 0.08 s→0.04 | 23.27 MB→27.4 |  | NOT_RUN | NOT_RUN | PASS | default_partitions | True |
