# 发给 A 的提示词

你是 PMUX 优化项目的 A：证明引擎负责人。B 已重新设计并验证 case3/case4
算法，现需要你冻结两类结构证明结果；不要修改 B 的 `pass/`、综合插入点、
测试判据或官方 RTL。

起点与材料：

```text
B 分支：feature/b-v02-case34
B 起点：3aff789a3badf6fecc4d3651e4b7eea52627fe3c
设计记录：pmux_project/reports/b-v02-case34-start.md
实验材料：pmux_project/experiments/case34_v02b/
```

已验证事实：

1. case3 官方基线 13 `$lut`，guard 候选 11 `$lut`；官方 RTL 对最终
   post-ABC9 gate 的 whole-design EQY PASS。
2. case4 正确官方表的严格可比结果为 36→27 post-ABC9 `$lut`、
   35→26 厂商组合原语；官方 RTL对最终 gate EQY PASS。
3. 历史 26 `$lut` 使用未映射 FF 的不同截点，不得作为同口径结论。

任务一：冻结 case3 `GuardedConeProof`。至少包含：

```text
module
guard_sink_cell
guard_facts
source_value
source_width
removed_bit_indices
comparator_cells
comparator_constants
comparator_full_fanout_snapshot
per_comparator_equivalent_under_guard
folded_data_bits
preserved_variable_bits
affected_cells
proven_all_obligations
proof_ids
status
reason_code
usage
```

必须证明：

- guard 在同一周期真实支配目标锥；
- 每个缩宽比较满足 `G ⇒ full_eq == reduced_eq`；
- 所有折叠数据逐位由同一 guard/比较事实推出；
- `default` 自由位及未更新寄存器位保持；
- 比较器全部扇出已覆盖，或比较器被保留；
- 不跨 DFF 传播，不把局部事实当全局事实；
- `x/z`、符号/宽度不明、冲突、UNKNOWN、预算耗尽均拒绝。

增加正反例：合法 MSB guard、guard 不支配、比较器额外扇出、只有部分位
可折叠、常量含 x/z、符号扩展不明、事实冲突、预算三边界。

任务二：冻结 case4 `ConstantTableProof`。至少包含：

```text
module
root_outputs
address_bits_lsb_first
output_bits_lsb_first
address_width
output_width
truth_table
source_cells
sequential_sinks
reset_enable_hold_contract
complete_binary_domain
proven_all_rows
proof_ids
status
reason_code
usage
```

必须证明：

- 真值表来自实际 RTLIL 连接，不读取文件名、模块名或手写 case 表；
- 地址和输出位序明确，32 个确定地址全部覆盖；
- 每个表项与原 mux 锥逐位相等；
- 时序 sink、复位、使能和未赋值保持边界不变；
- x/z、多驱动、地址缺失、表冲突、锥越界和预算耗尽保守 UNKNOWN。

接口流程：先提交只含接口定义的独立提交，由 B 审查并明确 cherry-pick；
实现另做提交。不得把证明逻辑复制到 `pass/`。

最终返回：分支、起点、完整提交哈希、接口文件、实现文件、原因码、正反例
结果、case3/case4 实际证明记录、工作树状态，并明确允许 B cherry-pick。
