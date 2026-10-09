# v0.2 constant-table 授权生命周期绑定修复（B）

日期：2026-10-10

分支：`feature/b-v02-unified-selection-gate`

起点：`1ba34ab14077df119cdfa5f9bca8d24c08a24ca6`

A 阻塞审计：内容提交 `47b430b5605960e0413b010e9a6ba87689684671`，最终 HEAD
`d62248d44178da328bab57bf76348326b6285d46`，结论 `BLOCKED`。

## 1. 阻塞与根因

A 在授权发布后删除一个 `pmux_case34_lut4`，再用完全相同的 module/cell 名、类型、参数、
属性和端口连接重建。重建前后内容快照 `payload_equal=1`，旧实现的真实 `synth_intel` 与
`pmux_opt_abc9` 都错误选择 `accepted_constant_table`。

内容序列化只能证明消费时的状态相同，不能证明目标仍是发布时的对象实例。本轮保留完整
内容快照，同时增加独立的授权代际和生命周期门。

## 2. 生命周期契约

生产者在 constant-table 原子提交完成后发布四项瞬态状态：

```text
pmux_opt.accepted_constant_table
pmux_opt.accepted_constant_table_snapshot
pmux_opt.accepted_constant_table_generation
pmux_opt.accepted_constant_table_lifecycle_valid
```

每次发布取得唯一 generation token，并注册只读 `RTLIL::Monitor`，记录该代际全部实际目标
cell 和所属 module 的对象实例。发生以下任一事件即永久把当前代际标记为无效：

- 已授权 cell 的任一端口连接被解除或改变；
- 已授权 cell 被删除；删除会先解除其端口，因而必然触发观察器；
- 包含已授权目标的 module 被删除。

消费者必须同时验证：授权存在、generation 非空、lifecycle 仍有效、目标集合非空、完整
内容快照一致。删除后即使 allocator 复用地址，或重建对象的全部可序列化内容相同，旧代际
已经被观察器置为无效，不能恢复。

成功和失败都在任何 techmap/ABC9 副作用前清除授权、快照、generation 和 lifecycle 状态。
旧观察器按 generation token 自动失活；同一插件路径在清理授权时同时从 Design 注销并释放
本插件拥有的观察器。runner 和真实 `synth_intel` 使用同一共享契约。

## 3. 最小反例结果

真实测试输出：

```text
PMUX_PROFILE_SAME_NAME_REBUILD payload_equal=1 lifecycle_valid=0
```

| 消费路径 | 修复后 profile | 拒绝原因 |
|---|---|---|
| 真实默认 `synth_intel` | stock | `target_lifecycle_mismatch` |
| `pmux_opt_abc9` runner | stock | `target_lifecycle_mismatch` |

合法未变更的首次消费仍为 `accepted_constant_table`。消费后完整 cell 重生再次调用仍为
`stock`。

## 4. 测试

执行：

```bash
python3 pmux_project/experiments/case34_v02b/scripts/check_profile_consumption.py
python3 pmux_project/experiments/case34_v02b/scripts/run_profile_snapshot_tests.py
```

结果：

- 静态契约：`PROFILE_AUTHORIZATION_LIFECYCLE_SNAPSHOT_CONSUME_REPLAY PASS`；
- 真实授权测试：`12/12 PASS`；
- 覆盖合法首次消费、参数位/宽度、端口、属性、类型、删除、增加、空快照；
- 覆盖真实 `synth_intel` 的消费前完全同名同内容重建；
- 覆盖 `pmux_opt_abc9` 的消费前完全同名同内容重建；
- 覆盖消费后的完整 cell 重生重放。

核心回归：

- production decision-proof mutation seam：`26/26 PASS`；
- Analyzer slow：`2118 / 29 / 60 / 50 / 81 PASS`；
- 真实微测试：`19/19 PASS`，structure/check/EQY/semantic 全部 PASS；
- 四例 runner 资源与 whole-design EQY：

| case | baseline `$lut`/DFF | production `$lut`/DFF | profile | EQY |
|---|---:|---:|---|---|
| 1 | 288/222 | 288/222 | stock | PASS |
| 2 | 25/0 | 25/0 | stock | PASS |
| 3 | 13/17 | 11/12 | stock | PASS |
| 4 | 36/10 | 27/10 | accepted_constant_table | PASS |

完整本机证据位于 Git 忽略目录：

```text
pmux_project/reports/b-profile-lifecycle-tests/
pmux_project/reports/b-profile-lifecycle-four-case/
pmux_project/reports/b-profile-lifecycle-micro-19/
/tmp/b-profile-lifecycle-mutations.log
```

## 5. 边界与门禁

本轮没有修改冻结证明接口、证明义务、官方 RTL 或测试期望。generation 只用于绑定一次授权
事务，不参与结构候选发现；没有增加 case、文件、module、signal、自动 cell 名或固定向量
身份分派。

状态：`READY_FOR_A_LIFECYCLE_BINDING_REAUDIT`。A 明确 `AUTHORIZED` 前，C 继续暂停最终
验收。A 授权后，C 必须从头完成两组 1000 种子和上一轮全部 `UNVERIFIED` 项。
