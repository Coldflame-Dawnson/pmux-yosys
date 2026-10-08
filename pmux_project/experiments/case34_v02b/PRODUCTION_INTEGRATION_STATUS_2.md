# case3/case4 v0.2 正式证明接入状态（B）

日期：2026-10-08  
分支：`feature/b-v02-case34-production`  
起点：B 证据提交 `4178495a04abdfef34dac1c79791aa132ba88610`  
A 接口/实现：原提交 `469f37b228adadc7d30aed93dc356bf705ca9830`、`143879d4d9079ebed8a26425958b5d2a61e77e86`

## 1. 本轮结论

1. A 的 `ProductionCase34Analyzer` 已接入冻结的
   `memory_map; opt -undriven -fine` 后、公共 `techmap` 前插入点。
2. case4 已由正式证明驱动通用 5×10 常量表提取、Shannon/cofactor
   重建和共享，严格 post-ABC9 结果为 36→27 `$lut`、10→10 `dffeas`，
   官方 RTL 对实际 gate 的 EQY PASS，达到修正后的严格 P2 oracle。
3. case3 已由正式证明驱动 3 个比较器 8→7 缩宽及 7 个数据位折叠，
   gate EQY PASS；但 post-ABC9 仍为 13 `$lut`/17 `dffeas`，没有达到
   实验候选的 11/12，故状态为 `P2 GAP`，不能进入默认获益路径。
4. case1、case2 在显式 v0.2 开关下均无结构候选、无结构写入，并记录
   `no_v02_case34_candidate`。匹配不读取 case、文件或模块身份。
5. `-v02-case34` 仍是隔离候选生成开关，不是默认开启；成本门和 EQY 门
   不得被绕过。

## 2. 生产改写边界

### 2.1 guard 下比较器缩宽

B 只在 A 的以下字段全部成立时改写：整体状态为 `Normal`、全部义务成立、
比较器等价、数据位已证明、扇出完整、顺序边界完整，以及
`no_cross_dff_propagation/reset_preserved/enable_preserved/hold_preserved`。
所有 affected cell 和 sequential sink 均在写入前做类型、参数、端口、连接、
属性快照复核；`keep` 直接拒绝。

### 2.2 常量表重建

B 不保存 case4 固定向量。真值表、地址位、逻辑输出位、源 cell 和顺序 sink
均来自 A 的只读证明。实现遍历所有结构允许的 split，使用确定性单点严格改善
搜索；cofactor 按 `(support, truth mask)` 跨输出共享。旧 mux 采用逐位活性分析，
实际删除 16 个，保留并缩宽 1 个仍有外部扇出的共享 mux；新增 26 个单元。
cofactor 先保持为 `pmux_case34_lut4`，在公共 techmap 后再降低到 `$lut`，避免
提前 techmap 造成映射回归。

## 3. 实测结果

| case | baseline `$lut`/DFF | production `$lut`/DFF | 结构计划 | gate EQY | P2 |
|---|---:|---:|---|---|---|
| case1 | 288/222 | 288/222 | 空；不匹配负例 | 本轮未重跑 | 不回归 |
| case2 | 25/0 | 25/0 | 空；不匹配负例 | 本轮未重跑 | 不回归 |
| case3 | 13/17 | 13/17 | 3 个比较器缩宽、7 位折叠 | PASS | GAP（目标 11/12） |
| case4 | 36/10 | 27/10 | 删除 16、保留缩宽 1、新增 26 | PASS | PASS |

正式输入哈希：

```text
test3.v 1551B9212622CCEF6019D6E77DE2C43DC8C80D267957B55A42121F34A08780E9
test4.v C0C5042928BD6BF568E2B7FB2193F2B6DB9C4193BA3A5378FC50A331D43422D7
```

本机忽略证据目录：

```text
pmux_project/reports/b-v02-case34-production-validation/
pmux_project/reports/b-v02-negative/
pmux_project/reports/b-v02-production-micro-19/
pmux_project/reports/b-v02-production-atomic/
```

可重复命令：

```bash
python3 pmux_project/experiments/case34_v02b/scripts/run_production_validation.py \
  --tool-bin /path/to/frozen/toolchain/install/bin \
  --official /path/to/ignored/competition_case \
  --out pmux_project/reports/b-v02-case34-production-validation
```

## 4. 回归状态

- A analyzer selftest：2116、29、60、50 assertions，全部 PASS。
- A 正式 case3/case4 报告：全部义务成立，且
  `PRODUCTION_PROOF_NETLIST_UNCHANGED 1`。
- B 默认真实微测试：19/19，结构、`check`、EQY、语义全部 PASS。
- B 测试专用快照失败：精确零写入，`check`、EQY 全部 PASS。
- 1000 固定种子：本轮未重跑；前一集成提交已有 1000/1000 PASS，新增路径
  为显式 opt-in，但仍必须由 C 在候选提交上独立重跑后才能验收。

## 5. 未完成门禁

1. case3 缺少足以授权完整下一状态决策锥重建、并让综合合法合并 5 个顺序位的
   通用证明结果；B 不会从实验 RTL 反推并擅自跨越该证明边界。
2. 统一候选选择必须把 baseline、默认映射候选和自定义 ABC 候选隔离，并仅在
   资源不回归且 whole-design EQY PASS 后选择；当前开关只生成候选，不默认启用。
3. C 必须独立完成 1000 种子、四官方 case、负例、快照与候选选择验收。
4. case3 达到 11/12 前，整个 v0.2 P2 不能宣告完成。
