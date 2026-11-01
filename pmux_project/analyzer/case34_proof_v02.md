# PMUX v0.2 case3/case4 只读证明引擎

状态：A 实现；不创建、删除或重连 RTLIL cell，不做 Shannon split 选择、ABC9
成本预测、whole-design EQY 或生产提交。

## 授权规则

B 只能在以下条件同时成立时消费证明：

```text
status == V02ProofStatus::Normal
GuardedConeProof::proven_all_obligations == true
```

或：

```text
status == V02ProofStatus::Normal
ConstantTableProof::proven_all_rows == true
complete_binary_domain == true
reset_enable_hold_contract 的四个布尔字段全部为 true
```

逐比较器、逐数据位或逐表行结果只用于诊断。UNKNOWN、事实冲突、三类预算
耗尽、快照不符、`x/z`、多驱动和时序边界不明都拒绝整个请求。首版不调用
SAT；SAT 原因码为未来接口保留，不能作为当前成功结果。

## GuardedConeProof

请求由 B 从实际结构构造：模块和 guard sink、当前周期 guard facts、比较源、
拟删除位、每个 `$eq/$ne` 的完整快照和全扇出、逐数据位折叠义务、必须保留
的自由位以及 affected-cell 集。A 从 guard sink 的输出沿 `$mux` 已知选中端反向
遍历，所有 guard fact 必须真实成为该锥内的选择条件，比较器也必须位于选中锥。
寄存器输出只可作为当前周期自由边界，证明不穿过 DFF。

比较宽度取实际 `A_WIDTH/B_WIDTH` 和端口；两侧 signedness 必须存在且相等。
常量必须完全二值。每个删除位由共享三值/比较器反推引擎证明在 guard 下与比较
常量相同，因此得到 `G => full_compare == reduced_compare`。每个折叠位另外在
`G && comparator_result` 下逐位证明。比较器实际全扇出必须与请求快照完全相同；
若有 affected set 外使用者，结果设置 `comparator_must_be_preserved=true`。

官方 case3 的 `stage_proc.il` 结构扫描产生三个独立 guard sink 证明，分别覆盖
低三位、位 0 和高四位：比较器数 `2/3/2`，折叠位数 `6/2/8`，保留自由位数
`3/1/4`；全部为 `Normal + proven_all_obligations`。三处均由实际结构推导删除
`source[7]`，完整宽度 8、缩宽后 7，没有读取模块名、信号名、常量身份或自动
cell 名作为触发条件。

## ConstantTableProof

首版接受由完全确定常量叶和等宽 `$mux` 构成的完整平衡二叉表锥。地址顺序从
结构推导：叶层选择是 LSB，根选择是 MSB；请求的
`address_bits_lsb_first` 必须逐位匹配。`output_bits_lsb_first[i]` 必须与
`root_outputs[i]` 的 SigMap 规范位相同。行号是地址的无符号值，
`truth_table[row][bit]` 是输出 bit `bit`，即 `RTLIL::Const` 的 LSB-first 编码。
任何 `Sx/Sz/Sa` 叶、缺层、不一致层序、多驱动或 source-cell 集不完整均拒绝。

每个 root bit 必须位精确地到达且只映射到一个请求中的顺序 sink；所有 sink 的
类型、参数和完整端口逐项快照。宽 `$mux` 的 A/B 位只传播到同索引 Y 位，选择位
才影响全部 Y 位。由此冻结 reset、enable、未赋值 hold 和 D/Q/clock 边界，并
明确不跨 DFF 推理。表证明只授权 B 使用返回表构造候选，不授权任何特定 split。

官方 case4 经 `proc; opt; memory_map; opt` 后恢复 31 个宽 10 `$mux` 的完整
5→10 表、32 行和 5 个顺序 sink。地址 0 为 `10'b1001110000`，资源使用为
31 cells / 310 normalized bits / 63 events；结果为
`Normal + complete_binary_domain + proven_all_rows`。本记录不裁决 27 `$lut` 与
26 厂商组合原语的 P2 口径。

## 稳定原因码

`v02_reason_code_name()` 返回：

```text
proved
guard_not_dominating guard_unknown guard_xz
comparison_width_unknown comparison_signedness_unknown
comparator_fanout_uncovered full_reduced_compare_not_equivalent
data_bit_unknown partial_data_fold preserved_bit_missing
table_extract_failed address_order_unknown output_order_unknown
table_incomplete table_value_conflict table_xz multiple_driver
sequential_boundary_changed reset_enable_hold_unknown fact_conflict
budget_cells_exhausted budget_bits_exhausted budget_events_exhausted
sat_counterexample sat_unknown sat_timeout invalid_request snapshot_mismatch
```

除 `proved` 外均不构成改写授权。额外比较器扇出不是静默失败：完整快照匹配时
明确要求保留比较器；快照遗漏或变化返回 `snapshot_mismatch`。

## 测试与报告

```sh
make -C pmux_project/analyzer test YOSYS_ROOT=<fixed-install>
make -C pmux_project/analyzer test-slow YOSYS_ROOT=<fixed-install>
make -C pmux_project/analyzer case3-report YOSYS_ROOT=<fixed-install>
make -C pmux_project/analyzer case34-report YOSYS_ROOT=<fixed-install>
```

新增自测覆盖合法/非支配/下一周期 guard、比较器额外扇出、部分比较/折叠、
自由位缺失、`x/z`、signedness、事实冲突、地址/输出反序、地址缺失/重复、
表 `x/z`、多驱动、不完整锥、source/sink/时序快照错误和共享三预算边界。
每次调用前后都比较 RTLIL 文本指纹。报告命令只扫描结构并输出记录，不改设计。
