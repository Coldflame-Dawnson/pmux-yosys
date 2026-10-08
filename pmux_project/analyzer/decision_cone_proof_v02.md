# PMUX v0.2 case3 完整下一状态决策锥只读证明

状态：A 实现，2026-10-08。分析位置保持为固定生产插入点：

```text
read_verilog <official RTL>
synth_intel -family cycloneiv -top <top> -run begin:map_ffram
opt -fast -mux_undef -undriven -fine -full
memory_map
opt -undriven -fine
[decision-cone proof / pmux_opt]
```

证明器不创建、删除或重连 RTLIL 对象，不运行 ABC9，不选择生产候选，也不授权
直接删除、合并或跨越任何寄存器。结果唯一授权范围是：在提交前复核所有快照后，
以返回的规范化 DAG 替换列出的 sequential cell 的 D 输入组合锥。

## 接口

新接口位于 `pmux_v02_decision_cone_proof_api.h`，不修改上一轮冻结的
`pmux_v02_production_proof_api.h`。核心类型为：

- `DecisionConeProofRequest`：模块、可选 support/sink/source 快照锚点、DAG 上限和共享预算；
- `DecisionConeProof`：外层 guard、support、比较器、选择关系、共享 DAG、affected sinks、
  unaffected sequential bits、完整 source/external consumer 快照及整体义务；
- `DecisionSinkProof`：逐 D/Q 位快照、D-observation guard、care/value 掩码、DAG 根、
  原锥、外部消费者和 reset/enable/hold/init/no-cross-DFF 结论；
- `DecisionSequentialBitSnapshot`：cell 类型、参数、全部端口连接、cell/Q-wire 属性及
  Q-wire 模块端口方向和编号；
- `DecisionDagNode`：拓扑排序的常量或 `input ? high : low` 节点。

B 只有在 `status == Normal`、`proven_all_obligations == true` 且所有细分义务为真时
才能消费结果。可选锚点非空时必须逐项精确匹配；任何变化返回 `snapshot_mismatch`。

## 证明模型

### 结构发现

证明先消费上一阶段通用生产证明发现的同源比较器、共同可删除 guard 位及全部受影响
sequential sinks，再从每个 D/EN/SRST/ARST 位反向遍历实际 RTLIL。遍历在 sequential Q
处停止。只支持语义明确的比较、mux/pmux、布尔和归约 cell；未知 cell、多驱动、环、
含 x/z 的值或无法归一化的别名均拒绝。

所有组合 source cell 的输出扇出都会枚举。cone 内消费者、目标 sequential sink 和
外部消费者分别记录；组合输出直接成为模块输出而无法由 cell 快照覆盖时拒绝。

### 当前周期与选择语义

完整有限域使用所有 D/control 组合叶进行枚举。某 bit 在一行中发生非 hold 行为，当且
仅当对 `Q=0` 或 `Q=1` 至少一个初值，原 sequential next-state 与 Q 不同。所有受影响
bit 的非 hold 行的公共不变量形成外层当前周期 guard；被删除的源位必须是其中的确定
二进制事实，因此不会跨 DFF 推理。

`$mux` 按 `S ? B : A` 记录显式优先关系。原生 `$pmux` 不具有覆盖优先级；全部 select
在完整域上逐对证明互斥，否则返回 `select_overlap_unproved`。同步/异步 reset、enable
优先级按实际 sequential cell 类型解释；`$sdffce` 保持 Yosys 的 enable-priority 语义。

### D-observation 等价

对 sink bit `i` 定义 `Obs_i(x)`：异步 reset 未激活，enable（若存在）激活，同步 reset
未支配，因而 D 确实决定下一状态。对 support 地址 `a`，证明器记录：

```text
care_i[a] = exists x: support(x)=a && Obs_i(x)
value_i[a] = unique D_i(x) over all such x
```

若同一地址出现两个不同 D 值，则返回 `fact_conflict`。care=0 只是 D 未被观察的 don't-care，
不能被解释为原值。规范化 DAG 是 care 集上的确定性 reduced decision DAG；缺失分支以
确定规则折叠，跨 sink 共享相同节点和根。

逐位核心公式为：

```text
forall x: Obs_i(x) -> OriginalD_i(x) == DagRoot_i(support(x))
forall x,q: !Obs_i(x) ->
  Next_i(OriginalD_i,x,q) == Next_i(DagRoot_i,x,q)
```

第二式显式检查 `Q=0/1`，覆盖 guard 外、default、reset、enable 和 hold。由于所有 clock、
reset、enable、Q、初始化属性和模块输出端口都保持原快照，结论不授权改动 sequential
cell；后续常量寄存器消除或等价寄存器合并只能由常规综合合法产生。

## 测试专用消费者

自测中的消费者只读取 `support_lsb_first`、拓扑 DAG 和每个 sink 根，对全部 care 行重建
Boolean 函数并与 `value_mask_lsb_address` 比较。它不读取模块名、信号名、case 身份、
固定常量或自动 cell 名，证明返回信息足以机械构造同类组合结构。

## 正式 case3 记录

固定插入点报告结果：

```text
source_width=8 support=7 fixed=1 comparators=3
relations=11 dag_nodes=13 affected_sinks=8 unaffected_sequential_bits=9
source_cells=12 external_consumers=0
structure=1 guard=1 selection=1 inside=1 outside=1
sink_bits=1 fanout=1 boundaries=1 dag=1 all=1
status=Normal reason=proved
usage=24 cells / 50 bits / 216390 events
```

结构恢复的外层 guard 是一个模块输入位为 1 且源向量最高位为 1；名称只在报告快照中
出现，不参与匹配。DAG 结果包含：

- 3 个 sink 的根为常量 1；
- 两个 sink 共享同一非恒定根；
- 另两个 sink 共享另一非恒定根；
- 剩余 sink 有独立的 default/selected-data 函数。

这正是允许后续综合产生常量化和等价寄存器合并所需的信息，但证明本身不删除 DFF。
报告命令同时输出 `DECISION_CONE_NETLIST_UNCHANGED 1`。

## 预算与失败

总预算继续冻结为：

```text
512 cells
8192 normalized bits
262144 propagation events
```

上一阶段结构证明的实际使用量先从同一请求预算中扣除，然后才进行完整域枚举、DAG
构建和复核。三类耗尽分别返回 `budget_cells_exhausted`、`budget_bits_exhausted` 和
`budget_events_exhausted`，状态为 `BudgetExhausted`，不产生任何改写授权。

其余稳定失败码包括 guard 缺失/不支配/部分/xz、比较宽度或 signedness 不明、选择重叠、
优先级不明、数据位未知、部分 sink、未覆盖扇出、多驱动、别名不明、未知 cell、顺序
边界变化、reset-enable-hold 未知、快照不一致和事实冲突。除 `proved` 外全部拒绝。

## 回归

```sh
make -C pmux_project/analyzer test YOSYS_ROOT=<fixed-install>
make -C pmux_project/analyzer test-slow YOSYS_ROOT=<fixed-install>
make -C pmux_project/analyzer case3-report YOSYS_ROOT=<fixed-install>
make -C pmux_project/analyzer case34-report YOSYS_ROOT=<fixed-install>
make -C pmux_project/analyzer production-case34-report YOSYS_ROOT=<fixed-install>
make -C pmux_project/analyzer decision-cone-report YOSYS_ROOT=<fixed-install>
```

新自测覆盖重命名同构正例、常量和分支数据变化、mux 优先关系、guard 缺失/部分覆盖、
pmux 重叠、额外可观察扇出、reset/enable/hold/init/output 快照变化、部分位、x/z、宽度、
signedness、多驱动、未知 cell、事实冲突、source/sink 快照不一致及三类预算耗尽。
