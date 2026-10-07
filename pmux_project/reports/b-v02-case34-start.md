# B：case3/case4 v0.2 启动记录

日期：2026-10-07

## 1. 分支与边界

- 分支：`feature/b-v02-case34`
- 起点：`3aff789a3badf6fecc4d3651e4b7eea52627fe3c`
- 本阶段先闭合算法和证据，再进入生产结构改写。
- B 不复制 A 的证明逻辑，不修改 C 的判据，不修改官方 RTL。
- A 尚未冻结 case3 guard/比较器证明及 case4 真值表证明接口，因此生产
  改写保持默认关闭；B 先冻结候选、快照、交易和拒绝需求。

## 2. 已验证算法

### case3

在外层 `cs=1 && sr[7]=1` 下：

```text
sr == 8'hAA  <=> sr[6:0] == 7'h2A，选中数据折为 8'h55
sr == 8'hFF  <=> sr[6:0] == 7'h7F，选中低四位折为 4'hF
sr == 8'hFE  <=> sr[6:0] == 7'h7E，选中高四位折为 4'hF
default 保留 gpioout[0] <= sr[0]
```

候选保留两个独立 `if` 的当前周期语义、移位寄存器、`sr_out` 保持和
`gpioout` 未更新位保持。固定工具链实测：

| 项目 | baseline | candidate |
|---|---:|---:|
| post-ABC9 `$lut` | 13 | 11 |
| 顺序位 | 17 | 12 |
| whole-design source EQY | — | PASS |
| official RTL 对 post-ABC9 gate EQY | — | PASS |

顺序位减少是综合合并等价状态的结果，不是跨寄存器传播；未增加 DFF/IO。

### case4

从官方 RHS 拼接
`{dataout[5:0],only_5b,datain_5bis7,data_dp,data_dn}` 自动提取 32×10
真值表。每个输出位枚举一个 Shannon split：

```text
f = x ? f1 : f0
key = (remaining_support, 16-bit truth mask)
```

常量 cofactor 全局折叠；相同 key 跨输出共享。固定种子 500 候选加一轮
严格改进邻域，共评估 544 个向量。正确表上的最好向量仍为：

```text
[3,3,1,4,4,4,0,2,4,4]
```

严格可比结果：

| 项目 | baseline | candidate |
|---|---:|---:|
| post-ABC9 `$lut` | 36 | 27 |
| 顺序位 | 10 | 10 |
| 厂商组合原语 | 35 | 26 |
| 厂商 `$not` | 1 | 1 |
| official RTL 对 post-ABC9 gate EQY | — | PASS |

历史“26 `$lut`”可在不执行公共 FF/techmap 的实验截点复现，但该截点保留
`$adff/$sdffce`，不能与官方完整流程的 36 `$lut` 直接比较。严格流程的第
27 个 `$lut` 是驱动 `only_5b` 的一输入反相器；厂商 `map_cells` 将其恢复
为 `$not`，所以候选是 26 个 `fiftyfivenm_lcell_comb` 加 1 个 `$not`。
不得把 26 个厂商原语写成 26 个 post-ABC9 `$lut`。

## 3. B 的生产计划

### 3.1 单入口与阶段

沿用 `pmux_opt` 单入口，规则由结构触发。case3 在
`memory_map; opt -undriven -fine` 后形成原子结构计划。case4 采用同一规则
框架的两阶段 lowering：

1. 早期从常量 mux 锥恢复真值表并形成只读计划；
2. 快照仍匹配且 A 授权后，生成带冻结 truth mask 的内部 cofactor cell；
3. 公共 techmap/FF 映射后，将内部 cell 确定性降低为 `$lut`；
4. ABC9 只映射末级 mux；
5. 成本不降、证明未知或快照不符时删除隔离候选并保留原设计。

两阶段使用同一事务 ID、源快照哈希和 affected-cell 集；不得依赖模块名、
文件名、源码行号或自动 cell 名。

### 3.2 提交前必须检查的快照

- module/cell 仍存在，类型、参数及完整端口未变；
- `SigMap` 后驱动、扇出和输出顺序与计划一致；
- 比较器的全部扇出在 affected-cell 集内或明确保留；
- case3 guard 和每个删位/折叠数据均有 A 的有效 proof ID；
- case4 地址宽、地址位序、输出位序、全部表项和时序 D/Q 边界一致；
- reset、enable、未赋值保持及 `keep` 属性没有变化；
- 无候选重叠、重复写入或 `rewrite_conflict`；
- cell/bit/event 预算没有耗尽；
- 隔离试改后的真实成本严格降低。

### 3.3 默认拒绝原因

```text
proof_interface_missing
guard_unknown
comparator_fanout_uncovered
partial_data_fold
table_extract_failed
table_incomplete
table_value_conflict
address_order_unknown
output_order_unknown
sequential_boundary_changed
reset_hold_semantics_unknown
budget_exhausted
snapshot_mismatch
rewrite_conflict
cost_not_reduced
```

## 4. 当前工程门禁

在 A 返回冻结证明接口、C 独立复核证据以前：

- 不提交 case3/case4 生产结构改写；
- 不更改 `synth_intel` 的默认入口；
- 不把实验向量或真值表硬编码进生产运行时；
- 只允许结构规划、接口消费端、快照预检和零写入拒绝测试进入 B 分支。

## 5. 本次实际交付与 B 启动状态

可复现证据包位于 `pmux_project/experiments/case34_v02b/`：

- `RESULTS.md`：算法、严格统计口径、EQY 结果和 case4 口径更正；
- `MANIFEST.md`、`SHA256SUMS`：附件用途、依赖和完整性；
- `toolchain.txt`：Yosys/ABC/EQY/SBY 的完整提交与二进制哈希；
- `scripts/`：完整时序候选、自动表提取、短验证与固定种子搜索；
- `evidence/`：阶段 IL/JSON、日志、实际 EQY 配置与四个 PASS；
- `search/`：544 个唯一候选的完整轨迹与获胜候选。

B 的生产任务已启动到“规则契约和提交门禁”阶段：本文件已冻结快照字段、
拒绝原因和两阶段 transaction 要求，并分别向 A/C 发出证明接口与独立验收
prompt。由于两项上游门禁尚未返回，本提交不加入结构改写代码；这是 P0 的
保守拒绝，不是把实验候选当成生产实现。
