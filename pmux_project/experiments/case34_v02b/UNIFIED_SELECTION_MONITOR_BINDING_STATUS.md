# v0.2 constant-table generation/monitor 交叉绑定修复（B）

日期：2026-10-10

分支：`feature/b-v02-unified-selection-gate`

起点：`ec86209a61f22020707b299f5175f02b8d71495d`

A 阻塞审计：内容提交 `18c7995aba4d6337a8d962fe2b700e2584ea13d2`，最终 HEAD
`600548ba9b5016acd9870c26c0e6522397145596`，结论 `BLOCKED`。

## 1. 阻塞与修复

上一版只要求 scratchpad generation 非空。把合法 token 替换为任意非空字符串后，
authorization、lifecycle 和完整内容快照仍可使两条消费者错误接受。

共享 helper 现在遍历当前 `RTLIL::Design::monitors`，只接受满足以下全部条件的
`LifecycleMonitor`：

1. monitor 仍注册在当前 design；
2. monitor 的 `design` 指针就是当前 design；
3. monitor 的不可变 generation 与 scratchpad generation 完全相等；
4. authorization 和 lifecycle 均为 true；
5. monitor 的 `active()` 再次确认当前 generation；
6. 完整目标内容快照仍匹配且目标非空。

因此非空字符串不再构成授权。真实 `synth_intel` 和 `pmux_opt_abc9` 继续调用同一个
`constant_table_authorization_matches()`，并在任何映射副作用前消费全部瞬态状态。

## 2. A 最小反例结果

测试只把合法 generation 替换成 `B-forged-generation`，其余授权状态、目标集合和快照不变。

| 消费路径 | monitor match | profile | 拒绝原因 |
|---|---:|---|---|
| source-loaded `synth_intel` | 0 | stock | `target_lifecycle_mismatch` |
| `pmux_opt_abc9` | 0 | stock | `target_lifecycle_mismatch` |

完全同名同内容删除重建、端口改变后恢复等上一轮反例仍由 lifecycle 单向失效门拒绝。

## 3. 验证

```bash
python3 pmux_project/experiments/case34_v02b/scripts/check_profile_consumption.py
python3 pmux_project/experiments/case34_v02b/scripts/run_profile_snapshot_tests.py \
  --tool-root /home/james/work/pmux-yosys/pmux_project/official_baseline/toolchain/install
```

- 静态共享契约：PASS；
- 真实授权矩阵：`14/14 PASS`；
- forged-generation 在两条真实消费路径均为 stock；
- 合法首次消费仍为 `accepted_constant_table`；
- 同名重建和消费后重放仍为 stock。

核心回归：

- production decision-proof mutation seam：`26/26 PASS`；
- Analyzer slow：`2118 / 29 / 60 / 50 / 81 PASS`；
- 真实微测试：`19/19 PASS`，structure/check/EQY/semantic 全部 PASS；
- 四官方 case：

| case | baseline `$lut`/DFF | production `$lut`/DFF | profile | whole-design EQY |
|---|---:|---:|---|---|
| 1 | 288/222 | 288/222 | stock | PASS |
| 2 | 25/0 | 25/0 | stock | PASS |
| 3 | 13/17 | 11/12 | stock | PASS |
| 4 | 36/10 | 27/10 | accepted_constant_table | PASS |

本机展开证据位于 Git 忽略目录：

```text
/tmp/b-profile-generation-binding-tests/
/tmp/b-profile-monitor-mutations.log
pmux_project/reports/b-profile-monitor-four-case/
pmux_project/reports/b-profile-monitor-micro-19/
```

## 4. 边界

本修复不改变候选发现、证明义务、冻结接口、官方 RTL或测试期望，不引入 case、模块、
文件、信号、自动 cell 名或固定向量身份分派。A 明确 `AUTHORIZED` 前 C 继续暂停。

状态：`READY_FOR_A_GENERATION_MONITOR_REAUDIT`。
