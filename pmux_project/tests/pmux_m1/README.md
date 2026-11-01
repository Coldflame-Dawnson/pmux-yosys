# PMUX M1 C 测试数据

`micro/expectations.json` 是独立于被测程序的期望清单。19 个 `.il` 文件覆盖规范的 16 类：第 14 类拆成 cell、规范化位、事件三个边界，另有 case3 风格 7 位正例。位号均为 RTLIL A/B 端口物理位，从 0 起；`branch` 是 `$pmux.B` 的分支号。

`scripts/generate_micro.py` 可逐字节重新生成这些输入和清单。生成器只构造测试数据，不承担证明。原因码按 A/B 的机器接口填写，运行器逐例核对 `stats.reason_codes` 和 `reason_counts`。

第 14c 例采用 A 自测公布的 512 个 `$logic_not` 同时消费 512 位控制向量的拓扑，另在 `$pmux.A[512]` 留一个独立未知位。实测触发 `budget_events_exhausted`，保留 512 个已经完成的 A 位证明，未知位不改写。`stats.analysis` 与 `reason_counts` 保存实际队列事件及原因。

`seeds_1000.txt` 固定为 0 至 999，每个种子仅由 Python `random.Random(seed)` 生成一个设计。100 例快速回归取前 100 行，阶段验收取全部 1000 行。失败 seed 通过 `--seed N` 一条命令复现。

真实联调从仓库根目录运行，使用 M0 冻结的 Yosys/EQY 路径和当前集成工作树编译出的 `pass/pmux_opt.so`。`scripts/real_pass_adapter.py` 加载 before RTLIL，运行默认 `pmux_opt -write-plan -write-stats`，写出 after RTLIL；plan/stats 是 Pass 原文件。

```sh
BIN=/home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/bin
TEMPLATE="python3 {project}/scripts/real_pass_adapter.py --before {before} --after {after} --plan {plan} --stats {stats} --plugin {project}/pass/pmux_opt.so --yosys $BIN/yosys"
python3 pmux_project/scripts/run_micro.py --mode external --external-kind real --bin "$BIN" --out pmux_project/reports/c-real-micro-19 --dut-template "$TEMPLATE"
python3 pmux_project/scripts/run_random.py --count 100 --mode external --external-kind real --bin "$BIN" --out pmux_project/reports/c-real-random-100 --dut-template "$TEMPLATE"
```

只有 19 例全通过后才运行 100 例。`run_atomic_precheck.py` 使用 B 的测试专用 `-stub-one -test-snapshot-mismatch` 验证零写入回退，它不属于真实算法通过数。真实回归只用上述默认 Pass 命令。任何 LUT 收益均保持 `NOT_RUN`，直到四个官方 case 的同口径收益测量完成。
