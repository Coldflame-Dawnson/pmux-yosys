# case3/case4 v0.2 生产集成预检

日期：2026-10-07  
角色：B  
状态：`BLOCKED_BY_PROOF_COVERAGE`，生产改写未提交、未推送、未默认启用

## 1. 预检范围

B 从 A 的实现提交 `64d3eacdfdc1386f80276045eb2d87e6f346e678` 建立独立分支
`feature/b-v02-case34-integration`，尝试在冻结的 `synth_intel` 插入点消费：

- `GuardedConeProof`；
- `ConstantTableProof`。

授权条件保持不变：证明状态必须为 `Normal`，且对应的全部证明义务布尔值为真。
UNKNOWN、冲突、预算耗尽、快照变化均不得改写。

## 2. 冻结生产插入点

```text
read_verilog <official RTL>
synth_intel -family cycloneiv -top <top> -run begin:map_ffram
opt -fast -mux_undef -undriven -fine -full
memory_map
opt -undriven -fine
[pmux_opt]
opt_clean
techmap -map +/techmap.v
```

## 3. 实际发现

### 3.1 case3

在正式插入点运行 A 原始 `pmux_case34_proof_report`：

```text
CASE34_GUARD_REPORTS 0
CASE34_PROOF_NETLIST_UNCHANGED 1
```

原因是 A 的实验 matcher 要求 `outer $mux.B -> inner $mux.B -> $pmux`。经过
`synth_intel` 前半程和冻结的优化序列后，该链已被折叠；guard 进入
`$reduce_and/$reduce_bool/$not` 和寄存器控制逻辑。B 不得自行复制证明逻辑或把
实验结论直接视为正式插入点授权。

### 3.2 case4

在相同插入点运行 A 原始报告插件：

```text
CASE34_TABLE_PROOF module=test4 address_width=1 output_width=2 rows=2
source_cells=1 sinks=1 status=1 complete=1 all=0
reason=sequential_boundary_changed
CASE34_PROOF_NETLIST_UNCHANGED 1
```

A 已证明的完整 5×10、32 行表来自另一条实验流程：

```text
read_verilog; proc; opt; memory_map; opt
```

正式 `synth_intel` 流程会将 mux 按位片拆分/合并，并改变 sequential sink 的物理
分组。旧 full-driver/table-root/sink 契约不能覆盖生产对象。

## 4. fail-closed 结果

- case3 没有结构证明授权；不提交 case3 改写。
- case4 的证明为 UNKNOWN/拒绝；不提交 case4 改写。
- 没有用 case 名、模块名、自动 cell 名、信号名、固定真值表或固定 split 向量
  绕过证明。
- 没有移动冻结插入点。

## 5. 已完成的安全回归

当前接入草案重新编译成功。未启用 v0.2 opt-in 的默认模式实际通过：

```text
19/19 微测试：PASS（structure/check/EQY/semantic）
100/100 固定种子：PASS（synth/structure/EQY）
```

完整 1000 种子和四官方 case 的生产回归必须等待 A 的正式插入点证明后执行。

## 6. 证据溯源修正

C 指出的 case3 source 哈希差异已复核。用当前已提交的候选脚本重跑后：

```text
case3 candidate source:
DD2EB7F827A7B8719914E972D054D12925D2109F4EDE33C65293BE69B887EC52

case3 post-ABC9 gate:
BB1026ED444F44A6D8859BB9A180186793B7A404E080DC5BDA0D062405A5954D

case4 post-ABC9 gate:
BD9D87E50E510E2EB1C12896D1FABA327104ADDA741E01DF4C084B202BE8EFC5
```

两例 source EQY 和实际 post-ABC9 gate EQY 均重新 PASS。精确返回码、用时、
gold/gate 哈希见 `evidence/summary.json`；配置和完整日志见对应 `*_eqy/` 目录。

## 7. 后续门禁

1. A 在冻结插入点补齐 case3 与 case4 证明并提供可 cherry-pick 提交。
2. B 只消费 A 的完整证明结果，完成结构 matcher、快照预检、隔离试改、成本门
   和原子提交。
3. B 跑 19 微测试、1000 固定种子、四官方 case、post-ABC9/厂商统计和
   whole-design EQY。
4. C 独立重放并 ACCEPT 后，才允许讨论默认启用或合并。

