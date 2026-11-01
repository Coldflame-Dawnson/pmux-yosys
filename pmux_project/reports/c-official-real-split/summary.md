# 官方四例回归 (real)

结构改写：PASS；EQY：PASS；LUT 收益：FAIL。

真实模式使用与冻结 synth_intel.cc 的 map_ffram 顺序相同的分段命令，并加载真实插件；生产版 Yosys 的加载方式待 B 确认。CPU 与峰值内存均取完整综合 Yosys 日志。

| Case | LUT 基线→优化 | DFF 基线→优化 | IO 基线→优化 | CPU s 基线→优化 | Peak MB 基线→优化 | 改写位 | Structure | PMUX position | EQY | Method | ABC error |
|---|---:|---:|---:|---:|---:|---:|---|---|---|---|---|
| case1 | 288→288 | 222→222 | 0→0 | 0.14 s→0.11 | 27.84 MB→34.1 | 0 | PASS | PASS | PASS | whole_design | True |
| case2 | 25→25 | 0→0 | 0→0 | 0.08 s→0.02 | 22.27 MB→29.0 | 0 | PASS | PASS | PASS | default_partitions | True |
| case3 | 13→13 | 17→17 | 0→0 | 0.07 s→0.03 | 22.65 MB→29.04 | 7 | PASS | PASS | PASS | whole_design | True |
| case4 | 36→36 | 10→10 | 0→0 | 0.08 s→0.05 | 23.27 MB→29.9 | 0 | PASS | PASS | PASS | default_partitions | True |
