# Decision-cone proof consumer 独立审计

日期：2026-10-08  
角色：A（证明契约与只读证明引擎）  
审计分支：`feature/a-v02-decision-consumer-audit`  
被审计 B 提交：`25217ab0b901c27a2ee9e56bd38c67819c19978f`  
结论：**BLOCKED**

## 1. 判定边界

官方 case3 的 11 LUT / 12 DFF 和 gate EQY PASS 已独立复现，但它只能说明当前官方
proof 实例和当前设计组合没有触发缺陷，不能替代通用 proof consumer 的授权边界审计。
本审计不修改 `pmux_project/pass/`，不改变 A 的冻结头文件，也不改变综合或 EQY 判据。

当前消费者具备以下正确基础：

- `case34_rewrite.cc:138-150` 检查整体 `Normal/Proved`、全部整体义务及
  no-cross-DFF/reset/enable/hold；
- `case34_rewrite.cc:263-303` 检查逐 sink 义务、affected/unaffected sequential
  boundary、cell/Q-wire 属性和模块端口方向，并拒绝 `keep`；
- `case34_rewrite.cc:1019-1054` 在首次写入前完成跨候选 cell 冲突仲裁和二次快照检查；
- 首次写入位于 `case34_rewrite.cc:1056` 之后，正常的 snapshot mismatch 路径为零写入；
- `case34_rewrite.cc:760-801` 按拓扑顺序实现 `S ? high : low`，常量、共享节点和未使用
  节点裁剪的语义正确；
- 生产代码未按 case 编号、文件名、模块名、源码位置、信号名、自动 cell 名、case3
  比较常量或固定输出向量启用规则。README 中的 `test3` 仅是命令示例，不参与匹配；
- 消费者只改 comparator 的组合端口和列出的 D 输入，不直接删除/合并 DFF，也不写
  clock/reset/enable/Q/init。12 DFF 是后续综合结果。

但下述三个最小反例族在 `build_decision_plan` 中可通过现有预检，因而不满足“任何异常
proof、UNKNOWN、冲突或快照不一致均在第一次写入前拒绝”的冻结授权要求。

## 2. 阻断项

### B1：sink 外层身份未与 sequential snapshot 交叉绑定

位置：`case34_rewrite.cc:263-290`、`585-596`、`804-839`。

构造：令一个 `DecisionSinkProof` 的 `sequential_snapshot` 指向已完整快照的 sequential
cell A/D[0]，但把冗余外层字段 `sink.cell_id/sink.d_bit` 改为 cell B/D[0]，并把
`sink.original_d` 设成 B 当前 D[0]。整体和逐 sink 证明布尔值保持真。

现状：建立计划时用 A 的 boundary 做完整检查，却未检查：

```text
sink.cell_id == sequential_snapshot.cell_id
sink.d_bit   == sequential_snapshot.d_bit
sink.q_bit   == sequential_snapshot.q_bit
sink.logical_sink_bit == sequential_snapshot.logical_sink_bit
```

二次预检和实际写入随后使用 B 的外层字段，因此 B 的 D 可在没有 B sequential snapshot
授权的情况下被替换。这不是诊断字段差异，而是写目标身份未绑定。

最小修复：在计划建立阶段逐项精确绑定上述字段，并以 boundary 作为唯一写目标；重复
logical sink、重复 `(cell,d_bit)` 或 snapshot cell id 不一致均拒绝。修复后需有一个
真实测试 seam 注入该 proof 变体，并断言零写入。

### B2：comparator 结论未与实际端口、常量、source/support 和扇出快照绑定

位置：`case34_rewrite.cc:229-239`、`600-625`、`641-668`。

构造：实际 cell 为 `$eq(source, 2'b11)`，删除 bit 1；proof 中同一 comparator 的
`comparator_constant` 改成 `2'b10`。现有建立阶段只检查 cell 类型和结论宽度，未比较
结论常量与实际常量。局部 atom truth 按 proof 常量计算，而写入后的 comparator 输出按
实际常量计算，因此可选择一个在 proof truth 上匹配、在物理输出上相反的 atom，错误
替换 sink D。

另一个更小的原子性反例是缩短 `comparator_constant` 或让未删除 source bit 缺少 support
映射。`apply_comparator_narrowing` 已先写 comparator，之后 `case34_rewrite.cc:660-661`
才以 `log_assert` 检查常量和映射；失败不再是“第一次写入前拒绝”。

消费者还没有复核 `a_signed/b_signed`、removed indices 的唯一性/范围、实际 A/B/Y 端口
宽度和参数、恰有一侧为确定二进制常量、另一侧逐位 SigMap 等于 `source_value`，也没有
消费 `comparator_full_fanout_snapshot`。当前 A analyzer 会生成一致数据，但冻结接口没有
授权消费者把相互矛盾的结论字段静默拼接使用。

最小修复：把物化阶段所有 comparator `log_assert` 前移为只读拒绝门；对实际 cell 与结论
做上述完整绑定；复核每个 fanout snapshot 均属于已覆盖 source/affected/external 集合；
任何 `$eq/$ne` signedness、x/z、别名、support 缺失或异常宽度均在写入前拒绝。

### B3：care/value 域形状和规模未验证

位置：`case34_rewrite.cc:201-228`、`633`、`675-681`。

令 `N = support_lsb_first.size()`。消费者没有显式检查：

```text
1 <= N <= 12
normalized_dag.size() <= 4096
care_mask.size() == value_mask.size() == 2^N
care/value 不含 x/z
```

最小反例：对 N=3 提供长度 1 的 care/value，唯一 care 行匹配常量 0。`matches` 只遍历
返回的短 mask，于是接受常量 0，未验证其余七个地址。长度超过 `2^N` 时 atom truth 又会
被越界索引。N 过大时 `1 << N` 还有 C++ 有符号移位未定义行为。当前 analyzer 的默认
request 的确限制 12/4096 并生成完整二进制 mask，但 consumer 没有在 proof 对象边界验证
这些前置条件。

最小修复：使用经过溢出检查的 `size_t rows = size_t{1} << N`，在任何写入前检查固定
12/4096 上限、两个 mask 的精确长度及二进制性。不得用返回 mask 的长度替代完整域长度。

## 3. 契约澄清：care/value 局部表达式定理

下述消费定理成立：

```text
若对全部 care_i[a]=1 有 E(a)=value_i[a]，且 proof 已证明 care=0 时 D 不被观察，
并保持 reset/enable/hold/Next 行为，则仅用 E 替换该 sink 的 D 不改变时序语义。
```

理由是逐状态分两类：care=1 时 D 值相等；care=0 时下一状态独立于 D，且 sequential
cell 的 clock/reset/enable/Q/init 和模块端口快照不变。该定理足以授权常量、单原子、
反相原子、双原子 AND/OR/XOR，以及找不到简单式时的完整 DAG 回退。

这里澄清两个容易混淆的边界，不改变旧字段含义：

- **完整长度且全零的 care mask** 可以接受；它表示该 D 在完整 support 域均不被观察，
  此时任意确定 E 都满足定理，但所有逐 sink 和 sequential boundary 义务仍必须为真；
- **长度为零或短于 `2^N` 的 mask** 不是“空 care”，而是无效 proof 形状，必须拒绝。

`support_lsb_first[i]` 是地址 bit i；当前原子生成、局部真值比较和 DAG Test 均遵守该位序。

## 4. 其余逐项审计

| 项目 | 结论 | 说明 |
|---|---|---|
| 整体授权门 | PASS | status/reason、全部整体布尔及 reset/enable/hold/no-cross-DFF 均检查 |
| 逐 sink 授权门 | PASS with blocker | 义务均检查，但写目标身份存在 B1 未绑定 |
| source/external 快照 | PASS | 类型、参数、全部端口连接、cell 属性二次检查；`keep` 拒绝 |
| sequential 快照 | PASS with blocker | boundary 内容完整，但 B1 可把外层写目标换到另一 cell |
| 原子提交 | BLOCKED | 正常冲突/快照失败零写入；B2/B3 可在 comparator 写后断言/越界 |
| comparator 缩宽 | BLOCKED | 缺实际常量/source/support/signedness/fanout 的交叉绑定 |
| care/value 定理 | PASS | 定理成立；消费前必须补 B3 的完整域形状门 |
| 局部表达式 | PASS | care 行上的常量、atom、反相、AND/OR/XOR 判定正确 |
| DAG 物化 | PASS | 拓扑、low/high、Test 极性、常量、共享和裁剪正确 |
| 顺序边界 | PASS with blocker | 生产写路径不改 DFF 控制/Q/init；B1 绕过目标快照 |
| P1 结构驱动 | PASS | 生产源码无 case 身份和固定 oracle 分派 |
| 预算/规模 | BLOCKED | analyzer 有预算；consumer 缺 12/4096 和 truth-domain 显式门 |
| duplicate proof ID | NEEDS REJECT | 当前不影响 Boolean 语义，但稳定审计链无法区分重复证据，应拒绝 |
| decision relation 明细 | NEEDS REJECT | 只信整体 `selection_semantics_proved`，未逐 relation 检查 reason/overlap/priority；其 cell 通常被 source snapshot 间接覆盖，但冻结明细不应被忽略 |

## 5. 对抗性夹具

`audit_decision_cone_consumer.py` 是 A 侧只读测试，不包含或修改 B 生产代码。运行：

```sh
python3 pmux_project/analyzer/audit_decision_cone_consumer.py \
  pmux_project/pass/case34_rewrite.cc
```

结果：13/13 审计断言通过，并得到预期 `BLOCKED`。覆盖：稀疏 care、全 care、完整全零
care、`$ne`、非最高 removed bit、support 重排、两个 comparator 同真值、反相原子、
AND/OR/XOR、无简单式 DAG 回退，以及 B1/B2/B3 所需静态门缺失。

A 的既有 81 条 decision-cone 断言继续覆盖：重命名同构、比较常量/分支数据变化、priority、
guard 缺失/部分覆盖、pmux 重叠、额外扇出、reset/enable/hold/init/output 快照变化、部分
sink、x/z、signedness、宽度、多驱动、unsupported cell、fact conflict、source/sink
snapshot mismatch，以及 cell/bit/event 三类预算耗尽。B 的 `keep` 拒绝位于
`case34_rewrite.cc:247,281`。这些反例均不产生 A 授权；但它们不能代替 B1-B3 的 proof
对象变异测试 seam。

## 6. 实际验证记录

固定工具链重建 `pmux_opt.so` 成功。未运行 544 候选搜索或 1000 seeds。

```text
A fast:
  readonly 2116 PASS
  branch 29 PASS
  case34 60 PASS
  production case34 50 PASS
  decision-cone 81 PASS

A slow:
  readonly 2118 PASS
  branch 29 PASS
  case34 60 PASS
  production case34 50 PASS
  decision-cone 81 PASS

official case3 decision proof:
  support=7, comparators=3, relations=11, dag=13, sinks=8
  source_cells=12, external=0
  usage=24 cells / 50 bits / 216390 events
  all obligations=1, reason=proved
  DECISION_CONE_NETLIST_UNCHANGED 1

official production replay:
  case3 13 LUT / 17 DFF -> 11 LUT / 12 DFF, gate EQY PASS
  case4 36 LUT / 10 DFF -> 27 LUT / 10 DFF, gate EQY PASS
```

既有 case3/case4 proof reports 也保持 `NETLIST_UNCHANGED 1`；case4 地址 0 仍为
`1001110000`。临时构建物和验证日志不提交。

## 7. 进入 C 的结论

**不允许以 `25217ab0...` 当前 consumer 进入 C 最终验收。** 最小解阻范围仅是 B 在
`case34_rewrite.cc` 补齐 B1-B3 的 fail-closed 预检，并增加 proof-object 变异测试，证明
所有失败均发生在第一次 RTLIL 写入前。建议同时补齐 relation 明细和 proof ID 唯一性门。

修复不需要改变 A 冻结头文件、care 定理、DAG 编码或生产插入点。修复后 A 应重新审计
新 B 完整提交；官方 EQY PASS 仍不能代替该复审。
