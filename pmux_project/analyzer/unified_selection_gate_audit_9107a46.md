# 统一默认选择门独立审计

日期：2026-10-09
角色：A（证明条件、证明消费边界与统一选择授权审计）
被审计 B 分支：`feature/b-v02-unified-selection-gate`
被审计 B 提交：`9107a466f74ce27269361ee26e99093c1b8fc93e`
父提交：`99dee059077e83969cede6e292dc1ea9139389e3`
最终结论：**BLOCKED**

## 1. 审计范围

本轮从唯一审计对象建立独立分支
`feature/a-v02-unified-selection-audit-9107a46`。A 没有修改
`pmux_project/pass/`、`techlibs/intel/`、冻结接口、官方 RTL、测试期望或 ABC9
策略；新增内容仅位于 `pmux_project/analyzer/`。

方法包括：

1. 审查 B 提交相对父提交的完整 diff；
2. 调用真实 `pmux_opt` 和 `pmux_opt_abc9` 的 16 项行为夹具；
3. 复跑 production mutation seam、A 的 13+12 项 consumer 夹具；
4. 复跑 analyzer 快速/慢速回归和正式只读报告；
5. 复跑四例默认路径、显式 baseline、资源计数和 gate EQY；
6. 对 runner 与 `synth_intel.cc` 的 scratchpad 生命周期逐语句比对。

## 2. 默认启用边界

### 2.1 通过项

`pmux_opt.cc:521-523` 的默认启用条件为：

```text
!disable_v02_case34 && !stub_mode_set && !test_decision_proof_mutations
```

并在每次调用开头先清除
`pmux_opt.accepted_constant_table`。行为验证确认：

- 无参数真实模式默认启用已验收结构规则；
- `-v02-case34` 是兼容写法，结果与默认模式一致；
- `-no-v02-case34` 禁用结构规则，case4 无 structural edit 且走 stock；
- stub、snapshot mismatch、conflict 和 mutation 模式均不执行结构改写；
- 旧 scratchpad 在上述每种模式进入时均被清除；
- 无候选、proof reject 和显式禁用均不产生 mapping 授权；
- budget/fact-conflict/UNKNOWN 的保守拒绝仍由 analyzer 慢速回归覆盖。

snapshot/conflict 测试必须与 stub 组合，因此被 `!stub_mode_set` 隔离。原 v0.1
bit-rewrite 的 analyzer、batch planning、snapshot precheck 和原子 commit 代码未被本提交
修改或放宽。

### 2.2 constant-table 授权产生：PASS

`pmux_opt.cc:543-560` 先调用已经 A/C 验收的结构 consumer，再只遍历
`case34_result.edits`。仅实际提交并记录为
`constant_table_cofactor` 的 structural edit 才把 scratchpad 置真。因此：

```text
pmux_opt.accepted_constant_table =
    当前真实 pmux_opt 调用存在已证明且已提交的 constant_table_cofactor edit
```

该定义在独立行为测试中成立。无候选、拒绝、禁用和测试模式均为 false。

## 3. 独立 mapper 路径

### 3.1 `pmux_opt_abc9`：PASS

`pmux_opt.cc:787-795` 同时要求：

1. scratchpad 为真；
2. 当前选择内存在 `pmux_case34_lut4` cell。

判定后第 795 行立即将 scratchpad 清零，然后才执行 techmap/ABC9。行为结果：

| 场景 | 结果 |
|---|---|
| 无授权、无 cell | stock |
| 无授权、已有 table cell | stock |
| 伪造授权、无 cell | stock |
| 合法 case4 授权与 cell | accepted_constant_table |
| 合法授权消费后再次调用 | 第一次 custom，第二次 stock |
| `-no-v02-case34` | stock |
| stub / snapshot / conflict / mutation | stock |
| proof reject | stock |
| case1 / case2 | stock |
| case3 decision-cone | stock |

A 行为夹具汇总：

```text
passed=16 checks=16
```

### 3.2 runner：PASS

`run_synth_intel_pmux.py` 默认调用真实 `pmux_opt`，显式 baseline 才附加
`-no-v02-case34`。post-ABC9 统一调用 `pmux_opt_abc9`，并要求日志中恰有一个与已提交
structural edit 对应的 profile marker。runner 因而使用上述双因子判定和消费清零路径。

## 4. 阻塞项：`synth_intel.cc` 未消费 scratchpad

`techlibs/intel/synth_intel.cc:266-271` 检查 scratchpad 和
`pmux_case34_lut4` cell，并把结果保存到
`pmux_constant_table_profile`。随后第 272 行直接进入 techmap；从 scratchpad 读取到
mapper 开始之间没有：

```cpp
active_design->scratchpad_set_bool(
    "pmux_opt.accepted_constant_table", false);
```

A 夹具对该源码区间的机器检查结果：

```text
"synth_intel_consumes_scratchpad": false
```

相比之下，runner 使用的 `pmux_opt_abc9` 在第 795 行明确清零。因此两条集成路径的
授权生命周期不一致，且不满足本轮冻结要求“mapper 消费授权后立即清零”。

### 4.1 最小可重放反例

1. 合法 case4 在集成 `synth_intel` 路径生成并消费一次 constant-table 授权；
2. mapper 将当前 table cell techmap 掉，但 scratchpad 仍保持 true；
3. 在同一 design 生命周期中重新出现一个 `pmux_case34_lut4` cell，例如此前未选择的
   module 后续被选择，或后续 pass 新增同类型 cell；
4. 调用 `pmux_opt_abc9`；
5. `pmux_opt.cc:792-804` 会把已消费但未清除的旧授权与新 cell 组合，选择
   `accepted_constant_table`。

这不是“仅伪造授权”或“仅伪造 cell”，而是对本应一次性的真实授权进行重放。首次错误
profile 选择发生在 `pmux_opt_abc9` 的双因子判定处；此前没有 RTLIL 写入，随后 custom
techmap/ABC9 才开始改变设计。

### 4.2 最小修复

在 `synth_intel.cc` 完成 scratchpad+cell 判定后、任何 techmap 前，无条件清零该
scratchpad。更稳妥的做法是让 runner 和内建 `synth_intel` 共用同一个“读取、验证并消费”
helper，并增加：

- 集成路径消费后 scratchpad=false；
- 合法授权只可使用一次；
- 消费后重新出现同类型 cell仍为 stock；
- 未选择 module 中存在同类型 cell的选择切换反例。

修复不需要修改冻结 proof 接口、证明引擎、改写规则或成本 oracle。

## 5. 证明门和身份无关性

B 提交没有修改 `pmux_project/include/`，也没有修改
`case34_rewrite.cc`。因此已经授权的整体义务、逐 sink、comparator、ordered support、
relation、fanout、sequential boundary、reset/enable/hold/init 和 snapshot 门槛保持不变。

回归结果：

```text
production mutation seam: 26/26 PASS
A 原行为变异:           13/13 PASS
A 有序结构夹具:         12/12 PASS

analyzer fast: 2116 / 29 / 60 / 50 / 81 PASS
analyzer slow: 2118 / 29 / 60 / 50 / 81 PASS
```

正式只读报告：

```text
case3 decision proof:
  status=Normal all=1
  usage=24 cells / 50 bits / 216390 events
  DECISION_CONE_NETLIST_UNCHANGED 1

case3 production proof:
  all=1, usage=12 / 25 / 195180
  PRODUCTION_PROOF_NETLIST_UNCHANGED 1

case4 production proof:
  5-bit address, 10-bit output, 32 rows
  address 0 = 1001110000
  all=1, usage=23 / 122 / 7784
  PRODUCTION_PROOF_NETLIST_UNCHANGED 1
```

对生产选择代码检索，没有发现 case 编号、模块名、文件名、源码位置、自动 cell 名、
case3 常量或固定输出向量参与启用或 profile 判定。P1 身份无关性本身通过。

## 6. 四例默认路径记录

| case | structural edit | profile | baseline -> default | gate EQY |
|---|---|---|---|---|
| 1 | 0 | stock | 288/222 -> 288/222 | PASS |
| 2 | 0 | stock | 25/0 -> 25/0 | PASS |
| 3 | 1 decision-cone | stock | 13/17 -> 11/12 | PASS |
| 4 | 1 constant-table | accepted_constant_table | 36/10 -> 27/10 | PASS |

四例计数检查和 P2 状态均为 PASS。这些结果验证当前 runner 候选，但不消除另一条
`synth_intel.cc` 集成路径的授权重放缺陷，也不替代 C 对修复提交的最终验收。

## 7. 命令摘要

```text
make -C pmux_project/pass ...
pmux_opt -test-decision-proof-mutations
make -C pmux_project/analyzer decision-consumer-reaudit ...
make -C pmux_project/analyzer unified-selection-audit ...
make -C pmux_project/analyzer test ...
make -C pmux_project/analyzer test-slow ...
make -C pmux_project/analyzer decision-cone-report ...
make -C pmux_project/analyzer production-case34-report ...
run_production_validation.py --repo . --tool-bin ... --official ... --out ...
c++ ... -fsyntax-only techlibs/intel/synth_intel.cc
```

`synth_intel.cc` syntax check通过。A 没有运行 544 候选搜索或 1000 seeds。

## 8. 最终判定

```text
BLOCKED
```

不允许 C 对 `9107a466f74ce27269361ee26e99093c1b8fc93e` 开始最终默认路径验收。
最小阻塞条件只有一个：内建 `synth_intel.cc` 路径必须在读取并决定 profile 后立即清零
`pmux_opt.accepted_constant_table`，并以行为测试证明授权不能重放。

## 9. A 交付元数据

- A 分支：`feature/a-v02-unified-selection-audit-9107a46`
- 起点：`9107a466f74ce27269361ee26e99093c1b8fc93e`
- 审计内容提交：`ed46943b6e3e7dbee4a7def028721447350697df`
- 内容提交已推送至
  `origin/feature/a-v02-unified-selection-audit-9107a46`
- 内容提交修改范围仅为 `pmux_project/analyzer/`
- 相对 B 起点，`pmux_project/pass/`、`techlibs/intel/` 和冻结接口均无 A 修改
- 最终元数据提交、远端 HEAD 一致性和干净工作树由最终交付消息给出
