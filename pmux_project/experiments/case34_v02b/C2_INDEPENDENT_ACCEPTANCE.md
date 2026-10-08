# C2 独立验收：B 的 case3/case4 正式生产候选

日期：2026-10-08。被测远端 `origin/feature/b-v02-case34-production` 的完整提交为 `b01a9f40653a7e74257434459348d3662eb0282b`，父提交为 `d198fc564f4e290caed83e46fdea9adca1a0b615`。本报告所在分支从被测提交新建，仅新增本报告；未修改 A 证明引擎、B 改写实现、官方 RTL、测试期望或冻结接口。所有结论取自本轮 C 工作树的重新构建和重新执行，未把 B 原有 PASS 文件当作验收结果。

**结论：`CASE4_CANDIDATE_PASS`；`CASE3_P2_BLOCKED`；`OVERALL_BLOCKED_BY_CASE3_P2`。** case4 是显式 `-v02-case34` 候选通过，并不表示已进入默认选择路径。case3 gate 等价，但 13 LUT/17 DFF 未达到 P2 的 11/12；在新的 A decision-cone 证明和 B 生产提交出现并重跑全部门禁前，不得宣布 v0.2 P2 完成。

## 输入、工具和拓扑

- `origin`：`https://github.com/Coldflame-Dawnson/pmux-yosys.git`。C2 分支从上述完整 B 提交创建；`0f98b6ed542e52ca9b2dabe70ee2113e76912965` 是其祖先。相对前一 B 证据提交 `4178495a04abdfef34dac1c79791aa132ba88610` 的改动仅位于 analyzer、证明接口、pass、runner 和 case34 实验文档/证据范围；官方 RTL、测试期望和冻结环境文件在该差分内无改动。
- 官方输入来自本机忽略目录 `pmux_project/pmux_case/competition_case`，与冻结清单 `official_baseline/environment/input_inventory.md` 的 SHA-256 一致：test1 `2D2DBA011B0E8799D0DE3790B69B7F01C6EB6AC665C4321B57B0378A4972FD13`；test2 `E43C9B437071488551CAF3121E227E86F4FE3190751F245BEAC3B7ECC3FDB146`；test3 `1551B9212622CCEF6019D6E77DE2C43DC8C80D267957B55A42121F34A08780E9`；test4 `C0C5042928BD6BF568E2B7FB2193F2B6DB9C4193BA3A5378FC50A331D43422D7`。
- 冻结 Yosys 0.69，git sha1 `9f75ca1f9`；ABC 1.01（该冻结二进制）；EQY/SBY v0.69。用冻结 `yosys-config` 先 `make clean`，再干净重建 `analyzer_selftest.so` 与 `pmux_opt.so`；二进制及工具哈希见忽略证据的 `KEY_SHA256SUMS`。
- 官方综合使用审计过的 runner，插入次序为 `memory_map; opt -undriven -fine; pmux_opt; opt_clean; techmap -map +/techmap.v; ...; ABC9`。case4 候选使用仓库生成证据中的 ABC `-W 300` 脚本；case1/2 的正式负例采用默认 ABC 映射。所有 gate 为实际 post-ABC9 RTLIL，不是源级候选。

## 独立执行结果

A 自测四组分别为 2116、29、60、50 assertions，全部通过。正式 case3/case4 证明报告中 `PRODUCTION_PROOF_NETLIST_UNCHANGED 1`；case3 证明 3 个比较器缩宽与 7 个数据位折叠，case4 证明 5×10、32 行、row0=`1001110000`、17 个源 mux。默认模式真实微测试 **19/19**，测试专用 snapshot 原子回退 **精确零写入**，其结构、Yosys `check` 和 EQY 均 PASS。仓库冻结 `seeds_1000.txt` 的 1000 个种子真实回归 **1000/1000**，失败 seed 清单为空；seed 文件 SHA-256 为 `8DB91B2EE25D579493DBC2CA66417CC945E215B5424349884013834D43DF7AC4`。

下表中 LUT 是 post-ABC9 顶层 `$lut` cell 数，DFF 是 `dffeas` cell 数；IO 是 JSON 顶层 input/output **位数**，不是 Intel I/O primitive。总 cell 为 JSON 顶层全部 cell 数。EQY 使用官方 RTL 为 gold、实际 post-ABC9 RTLIL 为 gate，四例均有工作目录 `PASS` 和完整日志。

| 用例 | 默认基线 LUT/DFF | 显式候选 LUT/DFF | IO 输入/输出 | 候选总 cell | whole-design EQY | 结构计划 |
|---|---:|---:|---:|---:|---|---|
| case1 | 288/222 | 288/222 | 259/26 | 510 | PASS | 空；`no_v02_case34_candidate` |
| case2 | 25/0 | 25/0 | 25/5 | 25 | PASS | 空；`no_v02_case34_candidate` |
| case3 | 13/17 | 13/17 | 3/9 | 30 | PASS | 1 个 `guarded_compare_narrowing`；`folded_bits=7` |
| case4 | 36/10 | 27/10 | 8/10 | 37 | PASS | 1 个 `constant_table_cofactor` |

四例候选 Yosys `check` 均为 0 problems，DFF/IO 均未增加。候选日志时间按 `End of script` 行读取；ABC 时间按日志 `ABC: elapse` 行读取，分辨率为 0.01 秒，不能当作高精度性能结论。

| 用例 | Yosys wall s | CPU user/system s | ABC elapsed s | Yosys peak MB |
|---|---:|---:|---:|---:|
| case1 | 0.16 | 0.09/0.04 | 0.03 | 34.99 |
| case2 | 0.07 | 0.02/0.02 | 0.01 | 30.38 |
| case3 | 0.62 | 0.57/0.00 | 0.02 | 30.85 |
| case4 | 0.09 | 0.05/0.01 | 0.01 | 31.52 |

本轮另保留了 case1/2 误用 case4 自定义 ABC 脚本的探索网表于 `official/case1`、`official/case2`：case2 得到 20 LUT/0 DFF 且 EQY PASS，但它只是**ABC 映射变体**，不是 case34 结构规则收益，也不属于上表要求的默认映射 25 LUT 基线/负例。它未经过统一四例成本选择，因此不能作为当前生产推荐结果。

## 计划和边界复核

- case1/2 的 plan `structural_edits=[]`、`changed_bits=0`、`changed_structures=0`，stats 含 `no_v02_case34_candidate`。这是无身份误匹配的两个负例。
- case3 插入点 plan 恰有一条 `guarded_compare_narrowing`，`folded_bits=7`；插入点 JSON 未增删 cell，顺序控制连接和端口均保持。post-ABC9 仍是 13/17，明确为 P2 GAP。
- case4 插入点证明给出地址宽 5、输出宽 10、32 行、row0=`1001110000`、源 mux 17。插入点完整 JSON 独立差分：删除 16 个 `$mux`、新增 26 个单元、保留并将 1 个共享 `$mux` 从宽 2 缩至宽 1。整个插入点原有 `$mux` 共 19 个，其中只有 17 个属于被证明的表源，不能把 19 写作表源数。plan 的每输出位 split 为 `[3,3,0,0,0,4,0,0,0,0]`，是 `choose_splits(proof.truth_table)` 的输出记录；源码在证明地址宽内枚举 split 并做确定性成本搜索，未把这组向量作为运行时输入。
- case4 插入点原有顺序 cell 数量与参数不变，端口定义不变。JSON 的一个 `$sdffce.SRST` 位 ID 从 21 变为 47，但两者同名为 `$auto$simple.cc:234:combine_resets$156`；因此**不能声称所有控制端的数值 bit ID 逐字不变**。实际 gate 的 whole-design EQY PASS 和 A 的 reset/enable/hold 证明覆盖功能边界。该别名重编号作为结构审计事实保留在 `insertion_audit.json`。
- 源码扫描未发现 `test1.v`～`test4.v`、case4 固定真值 `1001110000` 或运行时固定 split 向量。生产入口按模块逐个获取证明束，依据已证明的结构与快照实施改写；自动 cell 名仅作为由当前网表证明生成的锚点，不用于预设名字分派。扫描结果与检查命令见 `identity_scan_exact.txt` 和证据脚本。这个源码审查是 P1 的证据，不把文本搜索误写成形式证明。
- `-v02-case34` 仍是显式 opt-in 候选开关；默认微测和随机回归未启用它。本轮没有把 case4 候选自动合入默认映射。后续必须统一比较 baseline、默认候选、自定义 ABC 候选，并在资源门和 whole-design EQY 门通过后再选择。

## 关键哈希与证据

post-ABC9 gate RTLIL 的 SHA-256：case1 `1C2549A01AAC386DC5CFD32D3F68DD3FEEB56EA48033AF0A8B59761AD689E508`；case2 `16C69C4C211B801EE8307E976C076BE48D8EBEA5219E6E303AC8FFE01E90B4ED`；case3 `8EDB5BC89176146B564BCD94A5DF9CE2C65F9119DAA4D055DF423FF64B0BABC8`；case4 `1FBCBB1646438F1EFED1FD7F7268CCD774A9C4A84527F5E5FD1CA4F07376395B`。全部关键阶段 IL/JSON、EQY 配置、完整 EQY work/PASS、构建/证明/综合日志、固定种子逐例目录、独立 audit 脚本与输出、`KEY_SHA256SUMS` 放在本机忽略目录：

`pmux_project/reports/c-v02-production-acceptance/`

其中 `official_default/case1|case2`、`official34/case3|case4` 是上表正式候选；`insertion/case3|case4` 保存 Pass 前后完整网表；`audit_summary.json` 和 `insertion_audit.json` 保存机器核对结果；`micro/`、`atomic/`、`random1000/` 保存回归；`official/case1|case2` 保存前述额外 ABC 变体。忽略目录不会随 Git push 上传，报告中的摘要和哈希留在提交中；若要远程独立复核原始日志，仍需单独传递该目录。所有证据从本轮 C 工作树生成，没有复用 B 的已有工作目录。

复现入口：使用冻结工具链执行 `make -C pmux_project/analyzer test production-case34-report`（指定实际 `YOSYS_ROOT`、`CASE3_RTL`、`CASE4_RTL`），用 `run_micro.py --mode external --external-kind real`、`run_atomic_precheck.py`、`run_random.py --count 1000 --mode external --external-kind real` 跑回归；用 `run_synth_intel_pmux.py --v02-case34` 跑四例，其中 case4 指定 `--abc-script experiments/case34_v02b/evidence/generated/abc.script`，再以本轮生成的 gate 运行 EQY。完整 argv、RTLIL、JSON、配置与日志均存于上述证据目录。