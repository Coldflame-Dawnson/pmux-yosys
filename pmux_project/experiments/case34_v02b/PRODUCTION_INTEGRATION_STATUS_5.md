# decision-cone consumer 二次复审解阻状态（B）

日期：2026-10-08

分支：`feature/b-v02-case34-production`

本轮起点：`44bafd2df3c469b036d4867acd77225a023a1c68`

A 二次复审内容提交：`21bd35319de40a83fcf45039fde4c3ded79ee181`

A 二次复审元数据提交：`81623bf96e1959e12ed13967c07ae9a3f632ec0d`

## 1. 修复结论

A 对上一提交给出 `BLOCKED`，指出 support 顺序、comparator 唯一性和 relation kind 三个
缺口。本提交仅修复这些预写授权门和相应测试，不改变冻结接口、证明器、正式插入点、
官方 RTL、测试期望或成本 oracle。

### 1.1 support 有序绑定

不再把 support 当作无序集合。对物理 comparator 的非恒定端执行 SigMap 后，严格按
`source_value` 的 bit index 递增遍历，并跳过 `removed_bit_indices`；第 k 个剩余 source bit
必须等于 `support_lsb_first[k].bit`。遍历结束时 support 数量也必须完全相等。

因此仅交换 `support_lsb_first[0].bit` 和 `[1].bit`、而不更新 DAG/care/value 的反例会在
第一次写入前拒绝。

### 1.2 comparator cell 唯一性

`build_decision_plan` 使用 cell ID 集合拒绝重复 comparator conclusion。合法 proof 的每个
物理 comparator 只能被缩宽一次，避免通用非最高 removed-bit 情形下二次缩宽。

### 1.3 relation kind 和完整多重性

- `MuxPriority` 严格要求实际 cell type 为 `$mux`；
- `PmuxMutuallyExclusive` 严格要求实际 cell type 为 `$pmux`；
- `SequentialControlPriority` 严格要求受支持的 sequential type，且 cell 必须属于 affected
  sequential boundaries；
- mux/pmux relation cell 集合必须与 source snapshot 中的 mux/pmux 集合完全一致且唯一；
- sequential relation 按 sink bit 计数，其 `(cell_id -> relation count)` 必须与 affected sink
  boundary 的多重性完全一致。这样保留 A 当前“每 sink bit 一条 relation”的合法格式，同时
  拒绝 relation 删除、重复和 sequential-cell substitution。

## 2. 对抗性验证

B production seam 扩展为 `26/26 PASS`。新增：

```text
support_reorder              -> reject, fingerprint unchanged
duplicate_comparator         -> reject, fingerprint unchanged
relation_kind                -> reject, fingerprint unchanged
sequential_relation_substitution -> reject, fingerprint unchanged
```

测试命令在与 A 相同的正式插入点执行：

```text
read_verilog test3.v
synth_intel -family cycloneiv -top test3 -run begin:map_ffram
opt -fast -mux_undef -undriven -fine -full
memory_map
opt -undriven -fine
pmux_opt -test-decision-proof-mutations
```

A 的 `decision_consumer_reaudit_plugin.cc` 未经修改，在临时目录直接包含本提交的生产
`case34_rewrite.cc` 后复跑：`13/13 PASS`。上轮三个失败项现在均为 reject 且完整 module
指纹不变；constant-side swap 继续按预期接受。

## 3. 完整回归

- analyzer 慢速：`2118 / 29 / 60 / 50 / 81` assertions 全部 PASS；
- 真实微测试：`19/19`，structure/check/EQY/semantic 全部 PASS；
- case3：`13 LUT / 17 DFF -> 11 LUT / 12 DFF`，whole-design gate EQY PASS；
- case4：`36 LUT / 10 DFF -> 27 LUT / 10 DFF`，whole-design gate EQY PASS；
- case1/case2 规则和路径未改，仍无 v0.2 case34 结构候选。

本轮生产 gate SHA-256：

```text
case3 62B9AEB533472D61A1230DF4525A2FC2F57E957188256C3D4B8E8CFD10A2A2EC
case4 B6A2D90C49C8522EFBF54BBE69603A9C6649935EADFE3C6A82490830B1C77E30
```

哈希相对前轮变化仍来自 `NEW_ID` 内的 C++ 源码行号；资源、结构语义和 EQY 结论不变。

本机忽略证据：

```text
pmux_project/reports/b-v02-consumer-reaudit-fix-production/
pmux_project/reports/b-v02-consumer-reaudit-fix-micro-19/
/tmp/b-reaudit-fix-mutations.log
/tmp/b-a-reaudit-fixed.log
```

## 4. 门禁

状态：`READY_FOR_A_SECOND_REAUDIT`。

在 A 对包含本报告的新 B 提交明确给出 `AUTHORIZED` 前，C 仍不得开始最终验收；统一默认
选择/成本/EQY 回退门仍不在本轮范围内。
