# PMUX v0.2 case1/case2 production candidates

本目录记录两条仅由结构触发的生产候选。它们不读取 case 编号、文件名、模块名、源码位置或自动 cell 名。

## 规则与门禁

- `paired shared-select PMUX reconstruction`：识别两个同宽、共享 `S` 的 `$pmux`；每个选择位必须由同一无符号状态向量与确定常量的 `$eq` 驱动，编码必须完整覆盖 `1..2^m-1`，并逐位证明偶/奇分支的交叉关系。整对原子替换为两棵共享状态选择树与末级交换。
- `four-branch priority mux LUT decomposition`：识别默认值为零、恰好四层、内部结果无额外扇出的 `$mux` 链；外层输出也必须没有未覆盖的 cell 消费者，以排除五分支链的内部四层。用每输出位三个 `cycloneiv_lcell_comb` 保留完整优先语义。选择信号不需要互斥，重叠时仍按原链由外至内的优先级求值。
- 配对 PMUX 对每个解码 `$eq.Y` 做完整扇出封闭检查。模块输出扇出、未知端口或待删除 PMUX 上未覆盖的端口一律拒绝；其他已知 cell 输入扇出仅在该 cell 不属于待删除二元组时允许，并在 proof reason 中记录为 `proved_with_preserved_decode_fanout`；因为比较器及这些消费 cell 都不会被改写，其连接和语义原样保留。
- 解码 `$eq` 的 `A_WIDTH/B_WIDTH/Y_WIDTH` 必须分别与实际 `A/B/Y` 端口宽度完全一致；参数缺失或不一致时，case12 全模块预检在生成任何候选或进入后续分析前以 `eq_port_width_mismatch` 整批拒绝，保证零写入。
- 分析只读；提交前重新比较 cell 类型、全部参数、属性、端口集合、连接和值。任一证明义务、保护属性、唯一驱动/扇出或快照检查失败均不改网表。
- `pmux_opt -v02-case12` 与 `synth_intel -pmux-v02-case12` 是显式候选入口。默认 `synth_intel` 不启用 case1/case2，等待 A 独立复审。

## 快速结构测试

```sh
make -C pmux_project/pass clean all \
  YOSYS_CONFIG=/path/to/yosys-config
python3 pmux_project/experiments/case12_v02/scripts/run_case12_validation.py \
  --yosys /path/to/yosys
```

测试覆盖 priority 链宽度 1/5 正例、三分支拒绝、五分支外层边界拒绝、内部节点额外扇出拒绝、`keep` 保护拒绝，以及配对 PMUX 解码比较器模块输出额外扇出和 `A_WIDTH`/端口宽度不一致拒绝。三个审计负例都必须产生零候选、零改写。成对 PMUX 的正式正例是官方 case1；必须从官方 RTL 重新生成 gate，并运行 whole-design EQY。

注：Yosys 0.69 的 `check`/`stat` 以及进程退出清理会自身解引用上述刻意构造的不一致宽度元数据。因此该单个畸形输入在执行 `pmux_opt`、断言零候选/零改写后，用测试专用 `design -reset` 丢弃设计；所有结构合法的其他夹具仍执行 `check; stat`。

## 当前本机证据

| case | 结构提交 | post-ABC9/厂商结果 | DFF | whole-design EQY |
|---|---:|---:|---:|---|
| case1 | 1 对 PMUX，删除 2、新增 18 | 182 `$lut` | 222 | PASS |
| case2 | 1 条四分支链，删除 4、新增 15 | 15 `cycloneiv_lcell_comb`；post-ABC9 `$lut` 为 0（已为厂商原语） | 0 | PASS |

case2 的 15 是厂商原语口径，不得表述为 15 个 `$lut`。默认未启用状态下，case1/case2 继续使用已验收 stock 结果 288/222 与 25/0。
