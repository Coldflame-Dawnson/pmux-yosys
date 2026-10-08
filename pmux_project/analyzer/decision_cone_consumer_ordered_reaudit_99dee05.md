# Decision-cone proof consumer 有序结构绑定复审

日期：2026-10-09
角色：A（证明契约与只读证明引擎）
被审计 B 提交：`99dee059077e83969cede6e292dc1ea9139389e3`
B 修复起点：`44bafd2df3c469b036d4867acd77225a023a1c68`
A 上轮复审内容：`21bd35319de40a83fcf45039fde4c3ded79ee181`
A 上轮复审元数据：`81623bf96e1959e12ed13967c07ae9a3f632ec0d`
复审结论：**AUTHORIZED**

## 1. 范围和方法

本轮仅审计 B 的 proof consumer，没有修改 `pmux_project/pass/`、冻结接口、官方 RTL、
测试期望或 ABC9 脚本。A 使用三层独立证据：

1. 逐路径检查 `case34_rewrite.cc` 的生产 `build_decision_plan`；
2. 原样复跑 A 上轮 13 项行为变异，并新增 12 项有序结构/完整性夹具；所有调用与生产相同
   的预写校验，均不调用 apply；
3. 复跑 B 的 26 项 production seam、analyzer 快速/慢速回归、正式插入点只读报告和
   case3/case4 production replay。

官方 EQY 结果只作为端到端补充证据，不替代 proof-object 授权边界检查。

## 2. 逐项结论

### 2.1 support 有序绑定：PASS

`case34_rewrite.cc:250-258` 按 source bit index 递增遍历，跳过
`removed_bit_indices`，并要求第 k 个剩余 source bit 经 SigMap 后严格等于
`support_lsb_first[k].bit`。最后同时要求 support 数量完全耗尽。

行为结果：

- 仅交换官方 proof 的 `support_lsb_first[0].bit/[1].bit`：拒绝，完整 module 指纹不变；
- synthetic removed bit 位于 0、2、3（首部、中部、尾部）：均接受；
- synthetic support 重排：拒绝；
- support alias：拒绝。

上轮的地址 42 语义反例仍能构造，但新生产预检在物化前拒绝该 proof。

### 2.2 comparator 唯一性和绑定：PASS

`case34_rewrite.cc:215-266` 逐项绑定实际 A/B/Y、WIDTH、A/B_SIGNED、唯一二进制常量端、
完整常量、SigMap(source)、`source_value`、removed indices、有序 support 和 Y 的完整
fanout snapshot。`case34_rewrite.cc:332-338` 另以 cell ID 集合拒绝同一 comparator
重复出现。

行为结果：

- 重复同一 `cell_id`：拒绝；
- removed index 重复、越界、support alias/reorder、常量、source、signedness、fanout
  任一变化：拒绝；
- 交换 comparator 常量端并由 analyzer 重新产生 proof：接受；
- 两个不同 comparator cell 使用相同 source/常量真值：均通过逐 cell validator，说明
  唯一性按对象身份而非内容哈希判断；
- comparator 缩宽只在全部计划预检和冲突仲裁后执行，不存在同一 cell 二次缩宽路径。

### 2.3 relation kind：PASS

`case34_rewrite.cc:351-398` 的类型绑定为：

```text
MuxPriority                -> 仅 $mux
PmuxMutuallyExclusive      -> 仅 $pmux
SequentialControlPriority  -> 仅受支持的 sequential cell，且必须属于 affected boundary
```

每项同时复核 reason、完整 cell snapshot、select 端口顺序、overlap、pairwise-disjoint 和
last-assignment 语义。relation kind、snapshot 或 select 变异均在写入前拒绝。

### 2.4 relation 完整性和多重性：PASS

生产预检从全部 source snapshots 计算期望 mux/pmux cell 集合，并要求实际 relation cell
集合精确相等；从全部 affected sink boundaries 计算每个 sequential cell 的期望 relation
计数，并要求实际多重性精确相等。

A 行为变异结果：

- 删除、重复或用另一 mux relation 替换：拒绝；
- 删除或重复 sequential relation：拒绝；
- 替换为 unaffected sequential cell：拒绝；
- 官方结构中一个 sequential cell 覆盖多个 sink bit：合法接受，多重性与 affected
  boundaries 精确一致。

冻结接口的 sequential relation 本身是 cell 级控制关系，没有 d_bit 字段；逐 bit 身份由
每个 `DecisionSinkProof.sequential_snapshot` 完整绑定，cell 级关系以受影响 bit 数量作
精确多重性核对。这不会把 relation 授权扩展到 unaffected bit。

### 2.5 sink、boundary、truth domain 和原子性：PASS

此前 B1/B3 门保持有效：

- sink 的 `cell_id/d_bit/q_bit/logical_sink_bit` 与 boundary 双向绑定；
- 重复 logical sink 和重复 `(cell,d_bit)` 拒绝；
- reset/enable/hold/init、cell/Q-wire 属性、模块端口方向和 D/Q 连接均复核；
- support 1..12、DAG 2..4096、care/value 长度严格为 `2^support_width` 且全二进制；
- full-size 全零 care 允许；零/短/长 mask 及 x/z 拒绝；
- source、external consumer、affected/unaffected sequential snapshot 均在首次写入前检查；
- 计划建立后二次 snapshot 变化由 `precheck` 拒绝；
- 所有 candidate 先完成计划建立与冲突仲裁，随后才进入 apply。失败路径为零写入。

B 的测试 seam 调用生产同一个 `build_decision_plan`，没有调用 apply，并逐项比较完整
module 指纹。

## 3. 行为测试结果

### 3.1 A 原 13 项夹具

```text
REAUDIT_SUMMARY passed=13 checks=13 failures=0
```

覆盖：care/value 长度、空 mask、support alias/reorder、relation snapshot/select/kind、
重复 comparator、额外 fanout、计划后二次 snapshot 变化、常量端交换。每项指纹均不变。

### 3.2 A 新增有序结构夹具

```text
ORDERED_REAUDIT_SUMMARY passed=12 checks=12 failures=0
```

覆盖：mux relation 删除/重复/替换，sequential relation 删除/重复/跨 unaffected 替换，
同 cell 多 bit，removed index 首/中/尾，synthetic support reorder，以及不同 comparator
cell 相同真值。

### 3.3 B production seam

```text
pmux_opt -test-decision-proof-mutations
passed=26 checks=26 failures=0
```

所有拒绝均发生在第一次写入前，完整 module 指纹不变。

## 4. analyzer 与只读报告

固定工具链结果：

```text
fast:
  readonly 2116 PASS
  branch 29 PASS
  case34 60 PASS
  production case34 50 PASS
  decision-cone 81 PASS

slow:
  readonly 2118 PASS
  branch 29 PASS
  case34 60 PASS
  production case34 50 PASS
  decision-cone 81 PASS
```

正式插入点 case3 decision proof：

```text
source_width=8 support=7 fixed=1 comparators=3
relations=11 dag_nodes=13 sinks=8 unaffected=9
source_cells=12 external=0
status=Normal reason=proved all=1
usage=24 cells / 50 bits / 216390 events
DECISION_CONE_TEST_CONSUMER reconstructed=1 constant_roots=3 shared_root_groups=2
DECISION_CONE_NETLIST_UNCHANGED 1
```

正式 production proof：

```text
case3: source_width=8 removed=1 comparators=3 folds=7 preserved=35
       sinks=8 all=1 reason=proved usage=12/25/195180
       PRODUCTION_PROOF_NETLIST_UNCHANGED 1

case4: address_width=5 output_width=10 rows=32 source_cells=17 sinks=10
       all=1 reason=proved usage=23/122/7784
       address 0 = 1001110000
       PRODUCTION_PROOF_NETLIST_UNCHANGED 1
```

默认实验拓扑报告也保持不回归，case3/case4 均为 proved，且
`CASE34_PROOF_NETLIST_UNCHANGED 1`。

## 5. production replay

```text
case3: 13 LUT / 17 DFF -> 11 LUT / 12 DFF
       production_count_check=PASS, gate EQY PASS

case4: 36 LUT / 10 DFF -> 27 LUT / 10 DFF
       production_count_check=PASS, gate EQY PASS
```

未运行 544 候选搜索或 1000 seeds。DFF 减少仍由后续综合产生；consumer 没有直接删除、
合并或跨越 DFF。

## 6. P1 与规模/预算

对生产 consumer 检索未发现 `test3/test4`、固定模块名、文件名、源码位置、自动 cell 名、
case3 常量或固定输出向量参与启用规则。实现只依赖 proof、cell 类型/参数/端口/连接、
SigMap 和 snapshot。日志中的 module/cell 名不参与规则选择。

support 上限 12 使 truth domain 最大 4096 行；移位使用 `std::size_t` 且发生在范围检查后。
DAG 上限 4096。A 的 cell/bit/event 预算耗尽与 fact conflict 测试仍由慢速 analyzer 回归
覆盖；任何非 Normal、非 proved、部分义务或预算失败 proof 都不能通过 `authorized`。

## 7. 最终判定

```text
AUTHORIZED
```

允许 C 对 B 提交 `99dee059077e83969cede6e292dc1ea9139389e3` 开始最终验收。授权范围仅为
冻结契约下、所有整体/逐 sink 义务为真且所有 snapshot 在提交前仍匹配的 decision-cone
D 输入组合逻辑替换；不授权直接删除/合并寄存器、跨 DFF 推理、忽略 UNKNOWN，或跳过 C
的 whole-design EQY 与成本验收。

A 分支、提交、远端同步和最终工作树状态在提交后元数据段补记。
