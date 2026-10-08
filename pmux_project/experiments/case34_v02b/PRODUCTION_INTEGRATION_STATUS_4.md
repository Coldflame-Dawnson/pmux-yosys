# decision-cone proof consumer 加固状态（B）

日期：2026-10-08

分支：`feature/b-v02-case34-production`

本轮起点：`25217ab0b901c27a2ee9e56bd38c67819c19978f`

A 审计提交：`8d0633b09c8eba2dfdff72bb332c5f0fe3bde330`

## 1. 结论

A 的上一轮结论为 `BLOCKED`。B 已修复审计报告中的 B1、B2、B3，并增加与生产
预写校验共用实现的 proof-object 变异自测。当前代码和实测可提交 A 重新审计，但在 A
明确给出 `AUTHORIZED` 前，不宣告最终授权，也不推进统一默认选择门。

## 2. 修复内容

### B1：sink 与 sequential boundary 交叉绑定

- 在建立计划时逐项核对 `cell_id/d_bit/q_bit/logical_sink_bit`。
- 同时核对 sink、boundary 的 cell 指针和 proof ID 关系。
- 拒绝重复 logical sink 和重复 `(cell_id,d_bit)`。
- 二次预检和实际 D 端写入只使用 `sequential_snapshot` 中的 cell/D bit。
- boundary 的 cell snapshot、属性、Q wire 属性和端口方向仍在首次写入前复核。

### B2：comparator 结论与物理结构完整绑定

首次 RTLIL 写入前逐 comparator 核对：

- cell 类型以及 A/B/Y 端口；
- A/B/Y width 参数和实际端口宽度；
- A/B signedness 与 proof；
- 唯一常量端、完整二进制常量和 `comparator_constant`；
- 非常量端经 SigMap 后与 `source_value` 全宽相等；
- removed indices 唯一、在范围内且 reduced width 一致；
- 每个未删除 source bit 与 support 恰好一一映射；
- comparator Y 的实际完整 fanout 与 proof snapshot 全项一致；
- comparator reason 必须为 `Proved`，且必须允许缩宽。

decision relation 也在提交前核对 reason、重叠检查、互斥/优先语义、cell snapshot、
select 次序和实际端口。

### B3：truth domain 与规模

- `1 <= support_width <= 12`；
- `2 <= normalized_dag.size() <= 4096`；
- `care.size() == value.size() == 2^support_width`；
- care/value 每一位必须为二进制；
- 行数和移位改用经过上限保护的 `size_t`；
- full-size 全零 care 合法，零长度或短 mask 拒绝。

此外，proof module、非空 comparator/relation/顶层 proof ID 均已绑定；顶层重复
proof ID 拒绝。relation 和 sink 的内容哈希 proof ID 允许重复，因为 A 当前生成器会对
结构/函数相同的不同对象生成相同 ID；对象身份由 cell/sink 交叉绑定独立保证。

## 3. proof-object 变异测试 seam

新增 test-only 命令：

```text
pmux_opt -test-decision-proof-mutations
```

它从真实 case3 插入点取得合法 proof，复制并变异授权字段，再调用与生产消费路径相同的
`build_decision_plan` 预写校验。每项都比较调用前后的完整 RTLIL module 指纹；该模式不调用
任何 apply 函数。

结果：`22/22 PASS`。覆盖合法 proof、full-size 全零 care、四项 sink 身份错绑、重复
logical/physical sink、错误 comparator 常量/source/signedness、重复/越界 removed bit、
fanout 缺失、care/value 短长度与 x/z、support/DAG 超限、relation 未授权和顶层重复
proof ID。所有应拒绝项均在第一次 RTLIL 写入前拒绝且指纹不变。

## 4. 回归结果

- A analyzer 快速自测：`2116 / 29 / 60 / 50 / 81` assertions，全部 PASS。
- B 真实微测试：`19/19`，structure/check/EQY/semantic 全部 PASS。
- snapshot 原子回退：零写入、check、EQY 全部 PASS。
- case1/case2 显式 v0.2：无结构候选、无 structural edit；资源保持 `288/222`、`25/0`。
- case3：baseline `13 LUT / 17 DFF`，production `11 LUT / 12 DFF`，gate EQY PASS。
- case4：baseline `36 LUT / 10 DFF`，production `27 LUT / 10 DFF`，gate EQY PASS。

本轮 gate SHA-256：

```text
case3 A9F4F4137F7374CAC20D925EBA280EA6735EBA8CB6D7A37B66210632F46EF67C
case4 1778DFA8CFB2ABC4637885CB469C84D9C3913696E1C7A7978789FD02AB7B6BD9
```

它们与上一轮字节哈希不同，仅因本文件新增代码使 Yosys `NEW_ID` 中的 C++ 源码行号变化；
逐行 diff 显示结构和连接不变，变化限于 `case34_rewrite.cc:<line>` 自动名称。资源统计及
whole-design EQY 结果均不变。

本机忽略证据：

```text
pmux_project/reports/b-v02-consumer-hardening-micro-19/
pmux_project/reports/b-v02-consumer-hardening-atomic/
pmux_project/reports/b-v02-consumer-hardening-production/
/tmp/b-decision-mutations.log
```

## 5. 门禁状态

当前状态为 `READY_FOR_A_REAUDIT`，不是最终 `AUTHORIZED`。下一顺序固定为：

1. A 基于本提交重新做 proof consumer 独立审计；
2. 仅当 A 返回 `AUTHORIZED` 后，C 才在本提交上重新执行最终独立验收；
3. A、C 均通过后，B 才进入统一选择/成本/EQY 回退门的下一实现阶段。

本轮没有修改冻结 proof 接口、官方 RTL、测试期望、ABC9 脚本或 case 身份匹配逻辑。
