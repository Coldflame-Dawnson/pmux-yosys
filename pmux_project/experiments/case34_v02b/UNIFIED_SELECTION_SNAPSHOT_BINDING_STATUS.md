# v0.2 constant-table 授权快照绑定修复（B）

日期：2026-10-09

分支：`feature/b-v02-unified-selection-gate`

起点：`621cb4e56b734da73cfb6161b951a375893e2d81`

C 阻塞提交：`1de7aaf96b3957f2ed54c7c6ca509b00c39d74a2`。C 的真实默认
`synth_intel` 反例在合法 constant-table 授权发布后翻转一个 LUT 参数位；旧消费者仍选择
`accepted_constant_table`。该反例的 EQY 状态是 `ERROR`，本报告不把它写成不等价证明。

## 1. 修复契约

授权不再是独立布尔值。生产者同时发布：

```text
pmux_opt.accepted_constant_table
pmux_opt.accepted_constant_table_snapshot
```

规范化快照覆盖全部 `pmux_case34_lut4`，并绑定：

- module 和 cell 的实际身份；
- cell type；
- 参数名称、宽度、flags 和逐位值；
- 属性名称、宽度、flags 和逐位值；
- 完整端口集合、连接、常量状态和物理位序；
- 目标 cell 集合及多重性。

`pmux_opt_abc9` 与真实 `synth_intel` 共享同一个实现。消费时重新构造实际快照，仅当授权
存在、期望快照非空、目标集合非空且实际快照完全相等时，才选择
`accepted_constant_table`。两项 scratchpad 在任何 techmap/ABC9 副作用前无条件清零。

快照拒绝后仍把现存 `pmux_case34_lut4` 按其实际参数降低为普通 `$lut`，随后运行 stock
ABC9；这保证保守回退路径可执行，但不会把已改变的网表宣称为等价。正常候选的正式等价性
仍由 whole-design EQY 决定。

## 2. 真实默认路径变异测试

命令：

```bash
python3 pmux_project/experiments/case34_v02b/scripts/run_profile_snapshot_tests.py
```

结果：`10/10 PASS`。

| 测试 | 预期/实际 profile |
|---|---|
| 合法首次消费 | `accepted_constant_table` |
| LUT 参数翻转一位 | `stock` |
| LUT 参数宽度改变 | `stock` |
| A 端口连接改变 | `stock` |
| 属性改变 | `stock` |
| cell type 改变 | `stock` |
| 删除一个目标 cell | `stock` |
| 增加一个保留目标 cell | `stock` |
| 快照字段缺失 | `stock` |
| 首次消费后完整 cell 重生并再次运行 | `accepted_constant_table -> stock` |

所有拒绝项均记录 `target_snapshot_mismatch`；首次和第二次消费后授权均为空。测试加载实际
生产 `pmux_opt` 和当前 `synth_intel.cc`，变异发生在生产者返回后、消费者执行前。

静态契约检查：

```text
PROFILE_AUTHORIZATION_SNAPSHOT_CONSUME_REPLAY PASS
```

## 3. 回归

- 插件构建：PASS；`synth_intel.cc` C++ syntax check：PASS。
- production decision-proof mutation seam：`26/26 PASS`。
- Analyzer slow：`2118 / 29 / 60 / 50 / 81 PASS`。
- 真实微测试：`19/19 PASS`，structure/check/EQY/semantic 全部 PASS。
- 四例 runner 及 whole-design EQY：

| case | baseline `$lut`/DFF | production `$lut`/DFF | profile | EQY |
|---|---:|---:|---|---|
| 1 | 288/222 | 288/222 | stock | PASS |
| 2 | 25/0 | 25/0 | stock | PASS |
| 3 | 13/17 | 11/12 | stock | PASS |
| 4 | 36/10 | 27/10 | accepted_constant_table | PASS |

本轮未运行 1000 种子长回归，留给 A 授权后的 C 最终验收完整执行。

本机 Git 忽略证据：

```text
pmux_project/reports/b-profile-snapshot-tests/
pmux_project/reports/b-profile-snapshot-four-case/
pmux_project/reports/b-profile-snapshot-micro-19/
/tmp/b-profile-snapshot-mutations.log
```

## 4. 边界与门禁

本轮没有修改冻结证明接口、官方 RTL、测试期望或证明义务；没有增加 case、文件、模块、
信号名、自动 cell 名或固定真值向量分派。目标 cell 的实际名字只作为刚发布快照中的对象
身份字段，不参与候选发现或规则触发。

状态：`READY_FOR_A_SNAPSHOT_BINDING_REAUDIT`。A 对本提交明确给出 `AUTHORIZED` 前，C 不得
继续最终默认路径验收；A 授权后，C 必须从头完成两组 1000 种子、完整重放和其余未验证项。
