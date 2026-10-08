# v0.2 case3/case4 统一默认选择门（B）

日期：2026-10-09

分支：`feature/b-v02-unified-selection-gate`

起点：`99dee059077e83969cede6e292dc1ea9139389e3`

A 授权：`3b9185c86e23b365445b6a31f5d914e0da7f293f`，最终元数据 HEAD
`6ff4bd10ddd8995fc5d7f3a65d7150c2a3f6da53`，结论 `AUTHORIZED`。

C 独立验收：`a45ba765e83c3df108c39b482f097b868c075fa0`，结论
`P0_PASS / P1_PASS / P2_PASS / PRODUCTION_CANDIDATES_ACCEPTED`。

## 1. 默认策略

真实 `pmux_opt` 模式现在默认运行已经通过 A/C 门禁的 case3/case4 结构规则。
`-v02-case34` 仅保留为兼容写法；`-no-v02-case34` 是基线和诊断回退，不属于正常
生产选择。stub 与 mutation 测试模式仍自动隔离结构改写。

选择过程只读取结构、证明状态、快照和当前批次的提交结果，不读取 case 编号、文件名、
模块名、自动 cell 名或固定向量。无匹配、证明拒绝、预算耗尽或预写检查失败时保持原网表。

## 2. ABC9 profile 绑定

constant-table 规则只有在证明和原子提交成功后，才设置瞬态
`pmux_opt.accepted_constant_table` scratchpad。`pmux_opt_abc9` 同时要求：

1. 该瞬态授权为真；
2. 当前设计实际含有本批次生成的 `pmux_case34_lut4`；

才会应用 cofactor techmap 和 C 已验收的 ABC9 脚本。授权被消费后立即清零。仅 decision-cone
改写或无结构改写的设计走 stock ABC9；预先伪造同名单元不能单独选择 custom profile。

`synth_intel` 源码中的默认流程采用相同判定，并提供 `-no-pmux-v02` 作为诊断回退。

## 3. 本轮验证

- 插件编译：PASS；`synth_intel.cc` 独立 C++ syntax check：PASS。
- A/B decision consumer mutation seam：`26/26 PASS`。
- analyzer fast：`2116 / 29 / 60 / 50 / 81 PASS`。
- analyzer slow：`2118 / 29 / 60 / 50 / 81 PASS`。
- 真实微测试：`19/19 PASS`，structure/check/EQY/semantic 全部 PASS。
- 固定种子真实回归：`1000/1000 PASS`。
- 四例默认统一路径：

| case | baseline `$lut`/DFF | default `$lut`/DFF | profile | official RTL whole-design EQY |
|---|---:|---:|---|---|
| 1 | 288/222 | 288/222 | stock | PASS |
| 2 | 25/0 | 25/0 | stock | PASS |
| 3 | 13/17 | 11/12 | stock | PASS |
| 4 | 36/10 | 27/10 | accepted_constant_table | PASS |

四例 production count check 和 P2 status 均为 PASS。case1/case2 的结构计划为空；case3
提交一个 decision-cone 候选；case4 提交一个 constant-table 候选。完整本机证据位于 Git
忽略目录：

```text
pmux_project/reports/b-v02-unified-four-case-current/
pmux_project/reports/b-v02-unified-micro-19/
pmux_project/reports/b-v02-unified-random-1000-final/
```

## 4. 改动边界

本轮没有修改冻结证明接口、官方 RTL、测试期望或 A 的证明义务。生产语义仍由
`case34_rewrite.cc` 中已经授权的消费者执行；本轮只把已验收规则纳入默认统一调度，并把
映射 profile 与真实、已证明的结构提交绑定。

状态：`READY_FOR_A_UNIFIED_SELECTION_AUDIT`。A 授权前，C 不应把本提交判为最终 G3。
