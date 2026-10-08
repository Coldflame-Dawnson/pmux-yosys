# Decision-cone proof consumer 加固复审

日期：2026-10-08  
角色：A（证明契约与只读证明引擎）  
被审计 B 提交：`44bafd2df3c469b036d4867acd77225a023a1c68`  
B 修复起点：`25217ab0b901c27a2ee9e56bd38c67819c19978f`  
A 上轮审计：`8d0633b09c8eba2dfdff72bb332c5f0fe3bde330`  
复审结论：**BLOCKED**

## 0. A 交付元数据

- A 复审分支：`feature/a-v02-decision-consumer-reaudit`
- A 审计内容提交：`21bd35319de40a83fcf45039fde4c3ded79ee181`
- 远端：`origin/feature/a-v02-decision-consumer-reaudit`
- 审计内容提交已推送；本地 HEAD 与远端 tracking 在该提交上一致
- 审计内容提交后工作树：干净
- 修改范围仅为 `pmux_project/analyzer/Makefile`、A 复审插件和本报告；未修改 `pass/`

## 1. 范围与方法

本轮只读检查 B 的加固提交，没有修改 `pmux_project/pass/`、冻结接口、官方 RTL、综合
驱动、ABC9 脚本或测试期望。审计同时使用：

1. 对 `case34_rewrite.cc` 的逐路径静态检查；
2. B 的 `pmux_opt -test-decision-proof-mutations`，确认 22 项测试调用生产同一个
   `build_decision_plan` 且不调用 apply；
3. A 专用 `decision_consumer_reaudit_plugin.cc`，直接包含被审计实现并调用同一个匿名命名
   空间内的生产预写校验，对 B 未覆盖的 proof/design 变异做行为检查；
4. analyzer 快速/慢速回归、正式 case3 只读报告和 case3/case4 production replay。

官方 case3/case4 仍然 gate EQY PASS，但该事实不替代通用 proof-object 边界检查。

## 2. 逐项结论

### 2.1 B1 sink/boundary：PASS

`case34_rewrite.cc:398-441` 已完成：

- `cell_id/d_bit/q_bit/logical_sink_bit` 全部交叉绑定；
- sink 和 boundary cell 指针绑定到当前 module cell；
- sink/boundary proof ID 关系检查；
- 重复 logical sink 和重复 `(cell_id,d_bit)` 拒绝；
- boundary cell snapshot、cell/Q-wire 属性和模块端口方向复核。

`case34_rewrite.cc:735-748` 的二次 precheck 和 `955-990` 的 D 更新只使用
`sequential_snapshot` 中的 cell/D bit，不再信任外层冗余写目标字段。B 上报的六类 B1
变异均拒绝且指纹不变。

### 2.2 B2 comparator：BLOCKED

`validate_comparator` 已正确检查：

- `$eq/$ne`、A/B/Y 端口及 A/B/Y WIDTH；
- A/B_SIGNED；
- 恰有一个完整二进制常量端；
- 实际常量等于 `comparator_constant`；
- SigMap 后的非常量端全宽等于 `source_value`；
- removed indices 唯一、范围和 reduced width；
- Y 的实际 cell fanout 与完整 snapshot 相等；
- 所有检查发生在 apply 之前。

但 `case34_rewrite.cc:250-263` 只验证未删除 source bits 与 support 是集合上的一一映射，
没有验证 `support_lsb_first` 的顺序。如下最小变异被生产预检接受：

```text
swap(proof.support_lsb_first[0].bit,
     proof.support_lsb_first[1].bit)
```

其余 `input_index`、proof ID、DAG、care/value、source 和 comparator 均不变。由于两个 bit
仍各使用一次，现有 `support_used` 检查通过；但物化时 DAG input 0/1 已接到相反的物理
信号。

在官方 case3 proof 上得到具体反例：

```text
sink=0
原 support 地址=42
原 value/care 期望=0
交换 support[0]/support[1] 后物理求值=1
build_decision_plan=ACCEPT
```

因此这不是纯诊断字段不一致，而是可以构造错误 D 函数的语义反例。

此外，把同一个 comparator conclusion 重复追加到 `proof.comparators` 也被接受。通用接口
允许 removed bit 不在最高位；同一物理 comparator 被 `apply_comparator_narrowing` 连续处理
两次时可能发生二次缩宽，故 comparator cell 身份也必须唯一。

最小修复：

1. 按 source bit 递增、跳过 removed indices 后，要求第 k 个剩余 source bit严格等于
   `support_lsb_first[k].bit` 的 SigMap，而不是只做无序集合匹配；
2. 拒绝重复 comparator `cell_id`；
3. 为 support reorder 和 duplicate comparator 增加生产 seam 反例并验证零写入。

### 2.3 relation：BLOCKED

reason、snapshot、select 顺序、overlap、pairwise-disjoint 和 last-assignment 布尔值已经
逐项检查，snapshot/select 变异均被拒绝。

但 `case34_rewrite.cc:352-358` 把 `MuxPriority` 和 `PmuxMutuallyExclusive` 合并到同一分支，
只要求实际 cell 是 `$mux` 或 `$pmux`，没有要求 kind 与实际类型一一对应。把官方 proof
中一个 `$mux` relation 的 kind 改为 `PmuxMutuallyExclusive` 会被接受。

`SequentialControlPriority` 分支也只要求 cell 存在，没有绑定到实际 affected sequential
boundary 或受支持的 sequential cell 类型。

最小修复：

```text
MuxPriority             -> cell type 必须严格为 $mux
PmuxMutuallyExclusive   -> cell type 必须严格为 $pmux
SequentialControlPriority -> cell 必须属于 affected sequential boundaries
```

并增加 kind mutation 与 sequential-cell substitution 的零写入反例。

### 2.4 B3 truth domain：PASS

提交在计算 `truth_rows` 前检查：

```text
1 <= support_width <= 12
2 <= normalized_dag.size() <= 4096
```

行数使用 `std::size_t(1) << support_width`。每个 sink 的 care/value 长度必须严格等于
`truth_rows`，且每一位均为 0/1。DAG node id、input index、low/high 拓扑范围均在物化前
检查。长、短、零长度和 x/z mask 行为测试均正确拒绝，无越界或未定义移位路径。

### 2.5 care 契约：PASS

- full-size 全零 care：接受且预检不写 RTLIL；
- 零长度、短长度、长长度：拒绝；
- care/value 中的 x/z：拒绝。

这与冻结定理一致：全零 care 表示完整 support 域上 D 均不被观察，不等同于缺失 truth
domain。

### 2.6 proof ID：PASS

顶层授权记录、support 和 DAG proof ID 非空且按身份要求唯一。relation/sink 的内容哈希
没有被错误地要求全局唯一，因此 A 对结构相同的不同 relation/sink 生成相同内容哈希时，
合法 case3 不会被误拒绝。对象身份仍由 cell/sink/boundary 字段绑定。

### 2.7 测试 seam：PASS（覆盖仍不足）

B seam 从真实正式插入点取得 proof，逐项复制/变异并直接调用生产的
`build_decision_plan`。它没有调用 `apply_decision_plan`；每项均用 RTLIL backend dump 比较
调用前后的完整 module 指纹。

实际结果：

```text
passed=22 checks=22 failures=0
```

其 22 项覆盖与 B 报告一致，但没有覆盖本轮发现的 support 顺序、relation kind 和重复
comparator，因而 22/22 不能推出完整授权。

## 3. A 额外行为变异结果

A 专用夹具调用同一个生产预写函数，结果：

| 变异 | 期望 | 实际 | 指纹 | 结论 |
|---|---|---|---|---|
| care 长一行 | 拒绝 | 拒绝 | 不变 | PASS |
| value 长一行 | 拒绝 | 拒绝 | 不变 | PASS |
| care 零长度 | 拒绝 | 拒绝 | 不变 | PASS |
| value 零长度 | 拒绝 | 拒绝 | 不变 | PASS |
| support alias | 拒绝 | 拒绝 | 不变 | PASS |
| support 重排 | 拒绝 | **接受** | 不变 | **FAIL** |
| relation snapshot 变化 | 拒绝 | 拒绝 | 不变 | PASS |
| relation select 变化 | 拒绝 | 拒绝 | 不变 | PASS |
| relation kind 变化 | 拒绝 | **接受** | 不变 | **FAIL** |
| 重复 comparator | 拒绝 | **接受** | 不变 | **FAIL** |
| comparator 额外 fanout | 拒绝 | 拒绝 | 不变 | PASS |
| 建立计划后二次 snapshot 变化 | 拒绝 | 拒绝 | 不变 | PASS |
| 交换 comparator 常量端 | 接受 | 接受 | 不变 | PASS |

汇总：`10/13 PASS，3 FAIL`。交换常量端时同步交换 A/B signedness，由 analyzer 重新产生
proof；这确认 comparator 绑定不依赖固定常量端方向。

## 4. 首次可能写入点

所有失败 proof 目前都在计划建立阶段被错误接受。生产提交阶段调用
`apply_decision_plan` 后，首次写入发生在 comparator 缩宽：

```text
case34_rewrite.cc:781  apply_comparator_narrowing(...)
case34_rewrite.cc:767-775  setPort/setParam
```

之后才会新增 Boolean 逻辑并在 `case34_rewrite.cc:990` 写入 sequential D。没有后续校验能
修复已接受的 support 顺序或重复 comparator。因此修复必须位于 `build_decision_plan`，
不能依靠 apply 中的断言或最终 EQY。

## 5. 回归和实际记录

固定工具链结果：

```text
analyzer fast:
  readonly 2116 PASS
  branch 29 PASS
  case34 60 PASS
  production case34 50 PASS
  decision-cone 81 PASS

analyzer slow:
  readonly 2118 PASS
  branch 29 PASS
  case34 60 PASS
  production case34 50 PASS
  decision-cone 81 PASS

official case3 decision proof:
  source_width=8 support=7 fixed=1 comparators=3
  relations=11 dag_nodes=13 sinks=8 unaffected=9
  source_cells=12 external=0
  status=Normal reason=proved all=1
  usage=24 cells / 50 bits / 216390 events
  DECISION_CONE_NETLIST_UNCHANGED 1

production replay:
  case3 13 LUT / 17 DFF -> 11 LUT / 12 DFF, gate EQY PASS
  case4 36 LUT / 10 DFF -> 27 LUT / 10 DFF, gate EQY PASS
```

默认只读分析前后 RTLIL 指纹完全一致。未运行 544 候选搜索或 1000 seeds。

对 B 的生产 consumer 源码执行 P1 检索，没有发现 case 编号、模块名、文件名、源码位置、
自动 cell 名、case3 常量或固定输出向量匹配。结构日志中的 module/cell 名不参与启用规则。

## 6. 最终门禁

```text
BLOCKED
```

不允许 C 对 `44bafd2df3c469b036d4867acd77225a023a1c68` 开始最终验收。B 至少需要修复：

1. support 与剩余 source bits 的有序逐位绑定；
2. comparator cell 唯一性；
3. relation kind 与实际 `$mux/$pmux/affected sequential` 类型的严格绑定；
4. 对应 production seam 反例及零写入指纹检查。

修复不需要修改冻结 proof 接口、care 定理、生产插入点或成本 oracle。B 提供新的完整提交
后，A 应再次做独立复审。
