# PMUX v0.2 case3/case4 正式插入点只读证明

状态：A 实现，2026-10-08。证明位置固定为：

```text
read_verilog <official RTL>
synth_intel -family cycloneiv -top <top> -run begin:map_ffram
opt -fast -mux_undef -undriven -fine -full
memory_map
opt -undriven -fine
[proof / pmux_opt]
```

证明器不创建、删除或重连 RTLIL cell，不选择 Shannon split，不执行生产改写、
ABC9 收益测量或 whole-design EQY。

## 兼容接口

`pmux_v02_production_proof_api.h` 只新增 `ProductionCase34ProofRequest`、
`ProductionGuardedConeProof`、`ProductionConstantTableProof` 和完整快照类型。上一阶段
冻结的 `pmux_v02_case34_proof_api.h` 字段及含义没有改变。

请求中的 expected address/output/sink 向量是可选的提交前锚点：为空时按结构发现；
非空时必须与当前 SigMap 位序、物理 state bit 和完整 cell 快照逐项相同，否则分别
返回 `address_order_unknown`、`output_order_unknown` 或
`sequential_boundary_changed`。

## case3 方法

1. 按同一规范化源向量分组 `$eq/$ne`，要求实际宽度、两侧 signedness 和完全二值
   常量明确。
2. 对比较常量的公共候选位逐一尝试缩宽，不读取模块、信号、case 或自动 cell 名。
3. 从比较器完整扇出追踪到实际 sequential sinks，快照全部受影响组合 cell、扇出和
   顺序 cell 的类型、参数、端口及连接。
4. 在至多 16 个局部自由二进制位上穷尽原始 next-state 与“仅该比较器忽略候选位”
   的反事实 next-state。每个物理 Q 位同时检查 `Q=0/1`，因此 enable inactive 的
   hold 不能掩盖差异。
5. `$sdffce` 按固定 Yosys 0.69 的 enable-priority 同步复位语义处理；其他同步复位
   cell 保持 reset-priority。异步 reset 总是先处理。
6. guard 根由实际 sequential EN 极性取得，并调用共享三值/比较器反推引擎证明
   当前周期 guard 推出被删除源位。传播遇到 DFF 即停止。
7. 直接由比较器选择的 `$mux/$pmux` 数据源逐位置记录 comparator、cell、port、
   port bit、源位和折叠常量；其他源位位置记录为必须保留。

生产 case3 结果：

```text
source_width=8 removed=1 comparators=3
folds=7 preserved=35 guard_cells=6 affected=16 sinks=8
all_cmp=1 all_data=1 fanout=1 boundaries=1 all=1
usage=12 cells / 25 bits / 195180 events
status=Normal reason=proved
```

三个比较器均为 8→7、反事实 next-state 等价、完整扇出覆盖且不要求保留原比较器。

## case4 方法

1. 从候选 `$mux.Y` 的每个规范化位独立反向遍历，允许不同输出位由不同宽度、不同
   cell 和共享位驱动。
2. 常量叶必须完全二值且每位单驱动。每个 mux select 对子锥 select 建立偏序；只有
   能导出唯一完整 LSB-first 总序时才接受地址位序。
3. 在完整 `2^address_width` 域逐位解释 mux DAG，任何缺行、环、未知 cell、`x/z`
   或多驱动均拒绝。
4. 从顺序 cell 的 D/EN/SRST/ARST 锥自动发现非地址 mode 位，并在其有限二进制域
   中寻找使所有 next-state 位确定且与逻辑表列一一对应的模式。
5. 每个逻辑输出位映射到确切的物理 sequential cell 和 Q bit；一个物理 cell 可以
   覆盖多个逻辑位。映射同时比较 `Q=0/1`，因此明确验证 update、reset、enable 和
   hold，不跨 DFF。

生产 case4 结果：

```text
address_width=5 output_width=10 rows=32
source_cells=17 sequential logical-bit mappings=10
complete=1 rows_proved=1 mapped=1 all=1
usage=23 cells / 122 bits / 7784 events
row[0]=10'b1001110000
status=Normal reason=proved
```

10 个逻辑位映射到 5 个实际顺序 cell；其中宽 6 cell 映射 6 位，其余四个 cell
各映射 1 位。返回表只授权 B 构造候选，不授权固定 split 或成本结论。

## 授权与失败

B 只能消费：

```text
ProductionGuardedConeProof:
  status == Normal && proven_all_obligations
  && all_comparators_equivalent && all_data_bits_proved
  && all_fanout_covered && all_sequential_boundaries_proved

ProductionConstantTableProof:
  status == Normal && proven_all_obligations
  && complete_binary_domain && proven_all_rows && all_output_bits_mapped
  && reset/enable/hold/no-cross-dff 全部为 true
```

guard 不支配、极性错误、可观察额外扇出、部分位、`x/z`、signedness/宽度不明、
事实冲突、位序不明、缺失/重复地址、多驱动、sink 快照变化或任一预算耗尽均为
UNKNOWN/拒绝，不产生部分改写授权。

预算继续冻结为：

```text
512 cells
8192 normalized bits
262144 propagation events
```

候选发现、有限域枚举、sink 映射和最终证明共享累计预算。未调用 SAT。

## 回归命令

```sh
make -C pmux_project/analyzer test YOSYS_ROOT=<fixed-install>
make -C pmux_project/analyzer test-slow YOSYS_ROOT=<fixed-install>
make -C pmux_project/analyzer case3-report YOSYS_ROOT=<fixed-install>
make -C pmux_project/analyzer case34-report YOSYS_ROOT=<fixed-install>
make -C pmux_project/analyzer production-case34-report YOSYS_ROOT=<fixed-install>
```

生产自测包括 reduce/not/EN 正例、非支配与反极性 guard、额外可观察扇出、部分
位、`x/z`、事实冲突、位片化 5×10 表、多个 sink/单 sink 多位、地址缺失或重复、
地址/输出位序冲突、reset/enable/hold 快照变化、多驱动和三类预算边界。所有分析
均比较调用前后 RTLIL 文本指纹；正式 case3/case4 报告均输出：

```text
PRODUCTION_PROOF_NETLIST_UNCHANGED 1
```
