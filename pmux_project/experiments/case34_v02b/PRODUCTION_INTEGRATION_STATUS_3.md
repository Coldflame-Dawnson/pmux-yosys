# case3 decision-cone 正式接入状态（B）

日期：2026-10-08  
分支：`feature/b-v02-case34-production`  
本轮起点：`b01a9f40653a7e74257434459348d3662eb0282b`  
A 接口原提交：`8f25388269f87812d0c16be1ecb2d45c4c99887f`  
A 实现原提交：`95903f25a8ddeb4af117171c5e047fd2602dc6ae`

## 1. 结论

A 的完整下一状态 decision-cone 证明已接入 B 的显式 v0.2 候选路径。正式结果：

| case | baseline | production candidate | gate EQY | P2 |
|---|---:|---:|---|---|
| case1 | 288 LUT / 222 DFF | 288 / 222；无结构候选 | 前轮 C PASS | 不回归 |
| case2 | 25 LUT / 0 DFF | 25 / 0；无结构候选 | 前轮 C PASS | 不回归 |
| case3 | 13 LUT / 17 DFF | **11 LUT / 12 DFF** | **PASS** | **PASS** |
| case4 | 36 LUT / 10 DFF | **27 LUT / 10 DFF** | **PASS** | **PASS** |

case3 实际 production gate SHA-256：
`B858DD8BA0FEBCF5D3B94DA557BC07BE9E36A7640AFE9DC1D0C7E52DB442DF7`。

case4 实际 production gate SHA-256：
`F6EF3BA8702E56B21B2A6E775F2F8C05319EB012D28E026B99337000B0C194DE`。

## 2. B 如何消费证明

1. 仅在整体和逐 sink 全部义务为 `proved`、reset/enable/hold/init/no-cross-DFF
   契约成立、source/external/sequential 快照完全一致且无 `keep` 时建立计划。
2. 所有计划先完成冲突仲裁及二次快照检查，再发生第一处 RTLIL 写入。
3. A 返回的 comparator 只在其 `equivalent_under_guard=true` 且
   `comparator_must_be_preserved=false` 时按证明给出的 removed bits 缩宽。
4. B 不改 clock、reset、enable、Q、初始化或模块端口，只替换 proof 列出的 D 位。
5. 默认物化共享 decision DAG。为避免 mux 链的映射损失，B 还执行一个有界、
   完全由 proof 数据驱动的局部表达式选择：
   - 原子仅来自 `support_lsb_first` 和已证明 comparator 输出；
   - 候选仅为常量、单原子/反相原子和两个原子的 AND/OR/XOR；
   - 对该 sink 的每个 `care=1` 地址逐行比较 `value_mask`；
   - 全部相等才采用，找不到则回退 A 的完整 DAG；
   - `care=0` 不参与等价捷径，其安全性来自 A 已证明的 D-unobserved/hold/reset
     外部行为契约。
6. case3 最终三个 sink 使用常量根，两组复用相同原子，其余根复用 support 或
   comparator；只需新增 1 个 OR cell。没有硬编码 `test3`、信号名、cell 名、
   比较常量或固定输出向量。

## 3. 验证

- A analyzer selftest：2116、29、60、50、81 assertions，全部 PASS。
- B 默认真实微测试：19/19，结构、check、EQY、语义全部 PASS。
- B snapshot 原子回退：零写入、check、EQY PASS。
- case1/case2 显式 v0.2 负例：`decision_cone_candidates=0`、
  `structural_edits=[]`、`no_v02_case34_candidate=1`。
- case3/case4：实际 post-ABC9 gate whole-design EQY PASS。
- case4 保持 27/10，无 decision-cone 误匹配。
- 1000 固定种子：本轮 B 未重跑；上一 B 提交已由 C 独立验证 1000/1000，
  新提交仍必须由 C 独立重跑后才能进入最终验收。

本机忽略证据：

```text
pmux_project/reports/b-v02-case34-production-validation-2/
pmux_project/reports/b-v02-decision-cone/
pmux_project/reports/b-v02-decision-negative/
pmux_project/reports/b-v02-decision-cone-micro-19/
pmux_project/reports/b-v02-decision-cone-atomic/
```

## 4. 尚未完成

`-v02-case34` 仍为隔离候选开关。四例 P2 候选已达到，但统一算法的最终默认启用
仍需：A 对 B proof consumer 的授权边界审计、C 的 1000 种子及四例独立复验，
以及 baseline/候选成本门与 whole-design EQY 门的统一选择/回退。任何一项失败均
保持默认路径，不得仅凭 case3/case4 的当前 PASS 宣告 G3 完成。
