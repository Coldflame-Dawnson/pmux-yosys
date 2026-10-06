# PMUX 官方四例证明机会诊断

- 诊断对象：集成提交 `2fbd97aee2427c1c521eece1694f7f5f779cdfb0`
- 证据：该提交内 C 的 `c-official-b-audited` 四例 `plan.json`、`stats.json`、`before_pmux.il`、`after_pmux.il`、完整日志和 ABC9 指标
- A 分支起点：`feature/a-proof-engine`，分析器提交 `445231c4c616781a957292699376cb73bca60037`
- 范围：只读诊断；未修改 Pass、综合脚本、官方输入、冻结接口或生产规则
- 记号：位号均为 Yosys 端口的最低位为 0；`B_i[k]` 是 `$pmux.B` 第 `i` 个宽度切片的局部位 `k`

## 1. 结论分类

### 当前规则已经覆盖

1. case1 的 360 个 `sequential_boundary` 全部在目标数据位的直接 `$dffe.Q` 驱动处停止；这是冻结边界的正确行为。
2. case2 的 12 个、case4 的 120 个 `fixed_point_unknown` 都正常到达工作队列固定点；没有预算耗尽、`fact_conflict` 或不支持 cell。现有 `$mux` 规则只有在 `S` 已知时才传播，`ConstEval` 只有在全部输入已知时才正向求值，所以对下一层仍自由的选择信号不能继续。
3. case3 的 7 个位全部由 `guard -> $eq(X,C)=1 -> X=C` 的 `eq_true_constant` 规则证明并原子写入；proof ID、来源比较器和目标端口均与 plan 一致。

### 理论可证明但当前规则缺失

1. case3 `$procmux$10`：在选择条件 `$procmux$11_CMP=(sr==8'hff)` 为 1 时，A、B 两臂的三个对应位都为 1，因此整个 3 位 `$mux` 恒等于 A，可整体旁路。
2. case3 `$procmux$18.B0`：在 `sr==8'hff` 时，`B0=sr[3]=1` 且默认臂 `A=sr[0]=1`；又因另一选择是 `sr==8'haa`，两比较器互斥，所以 B0 分支可删除而保持选择语义。
3. case3 `$procmux$27`：位 0、1、2 在 `sr==8'hfe` 时分别与 A 位相等，可按位旁路；位 3 在 guard 下是 `B[3]=1`、`A[3]=0`，不能旁路。
4. 上述是“guard 下两表达式相等”的关系证明。v0.1 的事实域只保存单个位的 `ZERO/ONE`，且提交器只替换数据端口位，不表达关系事实、Y 位旁路、cell 删除或 PMUX 分支删除。

case2/4 的 `fixed_point_unknown` 不属于漏掉的常量证明：在各自局部 guard 下，剩余自由输入确实能让目标位取 0 和 1。case2 的反例直接来自自由的 `data*` 位；case4 的反例来自 ROM 表中剩余地址组合对应的 0/1 叶子。

### 即使证明也预计没有 LUT 收益

1. case3 当前 7 次改写仅把被选数据位换成常量，没有改变 `S/Y`、分支数、cell 数、DFF 或比较器扇出。ABC9 前只是改变真值表达式的写法。
2. `$procmux$11_CMP` 除了驱动 `$procmux$10/$procmux$18`，还进入 `$reduce_bool` 生成写使能；`$procmux$19_CMP` 除了驱动 `$procmux$27`，还进入写使能逻辑并直接驱动 `$sdffce.SRST`。因此七个位的证明本身不能删除两个比较器。
3. 这些 guard 蕴含关系在原网表中已经存在，ABC9 可以在映射时看到相同布尔函数。将 `sr` 位显式写成常量未降低函数支持集或剩余选择深度，四例结果因而保持 `288/25/13/36` LUT。
4. case1 不允许跨寄存器传播；当前周期选择 guard 不能被当作寄存器 Q 的同周期定义。即使猜测某些寄存器值，也既不构成合法证明，也没有可靠的组合 cell 删除收益。

### 值得进入 v0.2 的候选规则

优先级建议为：整 cell 的 guarded-arm equivalence；PMUX 冗余分支；带成本门的部分位旁路；小型 ROM/mux 树规范化。SAT 仅作为结构证明未覆盖时的受限后备，不应先于这些规则。

## 2. 总体计数与固定点状态

| case | cells / entries / target bits | 证明结果 | UNKNOWN 原因 | 改写 | ABC9 LUT |
|---|---:|---|---|---:|---:|
| case1 | 3 / 48 / 385 | 0 | `sequential_boundary=360`, `primary_input=25` | 0 | 288→288 |
| case2 | 4 / 8 / 35 | 0 | `fixed_point_unknown=12`, `primary_input=23` | 0 | 25→25 |
| case3 | 3 / 7 / 15 | `proven_one=7` | `sequential_boundary=8` | 7 | 13→13 |
| case4 | 19 / 38 / 152 | 0 | `fixed_point_unknown=120`, `primary_input=32` | 0 | 36→36 |

四例所有入口状态均为 `normal`，`sat_queries=0`，没有预算耗尽、事实冲突、改写冲突或失败提交。

## 3. case1：全部 sequential_boundary

总计 360 位，恰好是两个 12 位 `$pmux` 的 15 个 B 分支：`2 × 15 × 12`。每个目标位从 PMUX 数据端口经 wire alias 直接到一个 `$dffe.Q`；局部组合锥为空，停止位置为深度 0 的该 `$dffe`。`$procmux$8` 和两个 PMUX 的 A 入口没有 `sequential_boundary`；其余 25 个未知位是 `primary_input`，不在本节。

下表每行的目标都是相应 `B_i[0:11]`，物理端口位置为 `B[12*i +: 12]`。guard 对两个 PMUX 相同。

| i | guard | `$procmux$317.B_i`：目标信号 → 停止 cell | `$procmux$334.B_i`：目标信号 → 停止 cell |
|---:|---|---|---|
| 0 | `$procmux$29_CMP=1` | `i7[0:11]` → `$auto$ff.cc:337:slice$535:$dffe.Q[0:11]` | `q7[0:11]` → `$auto$ff.cc:337:slice$530:$dffe.Q[0:11]` |
| 1 | `$procmux$10_CMP=1` | `q7[0:11]` → `$auto$ff.cc:337:slice$530:$dffe.Q[0:11]` | `i7[0:11]` → `$auto$ff.cc:337:slice$535:$dffe.Q[0:11]` |
| 2 | `$procmux$11_CMP=1` | `i6[0:11]` → `$auto$ff.cc:337:slice$525:$dffe.Q[0:11]` | `q6[0:11]` → `$auto$ff.cc:337:slice$505:$dffe.Q[0:11]` |
| 3 | `$procmux$12_CMP=1` | `q6[0:11]` → `$auto$ff.cc:337:slice$505:$dffe.Q[0:11]` | `i6[0:11]` → `$auto$ff.cc:337:slice$525:$dffe.Q[0:11]` |
| 4 | `$procmux$13_CMP=1` | `i5[0:11]` → `$auto$ff.cc:337:slice$515:$dffe.Q[0:11]` | `q5[0:11]` → `$auto$ff.cc:337:slice$540:$dffe.Q[0:11]` |
| 5 | `$procmux$14_CMP=1` | `q5[0:11]` → `$auto$ff.cc:337:slice$540:$dffe.Q[0:11]` | `i5[0:11]` → `$auto$ff.cc:337:slice$515:$dffe.Q[0:11]` |
| 6 | `$procmux$15_CMP=1` | `i4[0:11]` → `$auto$ff.cc:337:slice$510:$dffe.Q[0:11]` | `q4[0:11]` → `$auto$ff.cc:337:slice$520:$dffe.Q[0:11]` |
| 7 | `$procmux$16_CMP=1` | `q4[0:11]` → `$auto$ff.cc:337:slice$520:$dffe.Q[0:11]` | `i4[0:11]` → `$auto$ff.cc:337:slice$510:$dffe.Q[0:11]` |
| 8 | `$procmux$17_CMP=1` | `i3[0:11]` → `$auto$ff.cc:337:slice$500:$dffe.Q[0:11]` | `q3[0:11]` → `$auto$ff.cc:337:slice$495:$dffe.Q[0:11]` |
| 9 | `$procmux$18_CMP=1` | `q3[0:11]` → `$auto$ff.cc:337:slice$495:$dffe.Q[0:11]` | `i3[0:11]` → `$auto$ff.cc:337:slice$500:$dffe.Q[0:11]` |
| 10 | `$procmux$19_CMP=1` | `i2[0:11]` → `$auto$ff.cc:337:slice$490:$dffe.Q[0:11]` | `q2[0:11]` → `$auto$ff.cc:337:slice$553:$dffe.Q[0:11]` |
| 11 | `$procmux$20_CMP=1` | `q2[0:11]` → `$auto$ff.cc:337:slice$553:$dffe.Q[0:11]` | `i2[0:11]` → `$auto$ff.cc:337:slice$490:$dffe.Q[0:11]` |
| 12 | `$procmux$21_CMP=1` | `i1[0:11]` → `$auto$ff.cc:337:slice$485:$dffe.Q[0:11]` | `q1[0:11]` → `$auto$ff.cc:337:slice$546:$dffe.Q[0:11]` |
| 13 | `$procmux$22_CMP=1` | `q1[0:11]` → `$auto$ff.cc:337:slice$546:$dffe.Q[0:11]` | `i1[0:11]` → `$auto$ff.cc:337:slice$485:$dffe.Q[0:11]` |
| 14 | `$procmux$23_CMP=1` | `i0[0:11]` → `$auto$ff.cc:337:slice$480:$dffe.Q[0:11]` | `q0[0:11]` → `$auto$ff.cc:337:slice$475:$dffe.Q[0:11]` |

结论：驱动结构都是 `dffe.Q -> alias -> PMUX.B_i[k]`，没有可在寄存器前继续追踪的组合逻辑。本报告不提出任何跨寄存器传播方案。

## 4. case2：逐入口 fixed_point_unknown

模块均为 `test2`。12 个 UNKNOWN 分成三个入口，每个位的首阻断 cell 都是深度 0 的内层 `$mux`。

| cell / entry / bit | guard | 深度 8 内局部组合锥 | 首个缺失事实 | 直接原因 |
|---|---|---|---|---|
| `$procmux$10/A/[1:4]` | `b=0` | d0 `$procmux$8`；d1 `$procmux$6`；叶：`c,d,datac[k],datad[k],0` | `$procmux$8.S=c` 未知；对应 Y[1:4] | guard 只固定 b，c/d 与数据位仍自由 |
| `$procmux$12/A/[1:4]` | `a=0` | d0 `$procmux$10`；d1 `$procmux$8`；d2 `$procmux$6`；叶：`b,c,d,datab[k],datac[k],datad[k],0` | `$procmux$10.S=b` 未知；对应 Y[1:4] | guard 只固定 a，b/c/d 与数据位仍自由 |
| `$procmux$8/A/[1:4]` | `c=0` | d0 `$procmux$6`；叶：`d,datad[k],0` | `$procmux$6.S=d` 未知；对应 Y[1:4] | guard 只固定 c，d 与 datad 位仍自由 |

这里的 `[1:4]` 精确表示四个目标位；对每个 `k∈{1,2,3,4}`，锥中的 `data*[k]` 是对应叶。现有 `process_mux` 在 `S=Unknown` 时立即返回，`ConstEval` 又因选择和数据输入未全知而不能求值。任取自由选择为 0/1、相应数据位为 0/1 即可构造目标位的两种值，因此不能把这些 UNKNOWN 升格为常量证明。

## 5. case4：逐入口 fixed_point_unknown

### 5.1 ROM mux 树记号

所有行的 module 都是 `test4`。为保留完整 cell 身份同时让表格可读，定义共同前缀：

`M = $memory$auto$proc_rom.cc:155:do_switch$8$rdmux`

| 别名 | 完整 cell 尾部 | 选择位 |
|---|---|---|
| R00 | `M[0][0][0]$56` | `datain[4]` |
| R10, R11 | `M[0][1][0]$59`, `M[0][1][1]$62` | `datain[3]` |
| R20..R23 | `M[0][2][0]$65`, `$68`, `$71`, `$74` | `datain[2]` |
| R30..R37 | `M[0][3][0]$77`, `$80`, `$83`, `$86`, `$89`, `$92`, `$95`, `$98` | `datain[1]` |
| R4C, R4E | `M[0][4][12]$137`, `M[0][4][14]$143` | `datain[0]` |

`T(R)` 表示以 R 为 d0 的、按对应输出位实际可达的 mux 子树；wire alias 可能跳层。完整最大层次为：

- `T(R00)`: d0 R00；d1 R10/R11；d2 R20..R23；d3 R30..R37；d4 R4C/R4E。
- `T(R10/R11)`: d0 R1x；d1 对应 R2x；d2 对应 R3x；d3 R4C/R4E。
- `T(R20..R23)`: d0 R2x；d1 对应 R3x；d2 R4C/R4E。
- `T(R30..R37)`: d0 R3x；d1 R4C/R4E。`T(R4C/R4E)` 只有 d0。

因此全部锥深度为 0..4，小于要求的 8 层。每个子树叶都是常量 0/1 和尚未被当前入口 guard 固定的低地址位。

### 5.2 30 个含 UNKNOWN 的入口

“bit→blocker.Y”同时给出每个目标位集合及其首个不可传播 cell 的输出位集合；两侧集合不暗示按书写顺序一一配对，但该行中的每个目标位都由同一个所列 blocker 首次阻断。同一行的所有位共享 guard 和所列 T 锥；T 是这些位的可达 cell 并集，各位实际锥是它的子集。

| # | cell / entry | guard | fixed-point UNKNOWN bit → 首阻断 | 局部锥 |
|---:|---|---|---|---|
| 1 | `R00/A` | `datain[4]=0` | `2..9 → R10.Y[1..8]`, `S=datain[3]` | `T(R10)`, max d3 |
| 2 | `R00/B` | `datain[4]=1` | `3,4,6..9 → R11.Y[1..6]`, `S=datain[3]` | `T(R11)`, max d3 |
| 3 | `R10/A` | `datain[3]=0` | `2..8 → R20.Y[1..7]`, `S=datain[2]` | `T(R20)`, max d2 |
| 4 | `R10/B` | `datain[3]=1` | `2..8 → R21.Y[1..7]`, `S=datain[2]` | `T(R21)`, max d2 |
| 5 | `R11/A` | `datain[3]=0` | `1,2,4..6 → R22.Y[1..5]`, `S=datain[2]` | `T(R22)`, max d2 |
| 6 | `R11/B` | `datain[3]=1` | `2..6 → R23.Y[1..5]`, `S=datain[2]` | `T(R23)`, max d2 |
| 7 | `R20/A` | `datain[2]=0` | `3..7 → R30.Y[1..5]`, `S=datain[1]` | `T(R30)`, max d1 |
| 8 | `R20/B` | `datain[2]=1` | `1,2,4..7 → R31.Y[1..6]`, `S=datain[1]` | `T(R31)`, max d1 |
| 9 | `R21/A` | `datain[2]=0` | `1,2,4..7 → R32.Y[1..5]`, `S=datain[1]` | `T(R32)`, max d1 |
| 10 | `R21/B` | `datain[2]=1` | `1..3,5..7 → R33.Y[1..5]`, `S=datain[1]` | `T(R33)`, max d1 |
| 11 | `R22/A` | `datain[2]=0` | `1..5 → R34.Y[1..5]`, `S=datain[1]` | `T(R34)`, max d1 |
| 12 | `R22/B` | `datain[2]=1` | `1,2,4 → R35.Y[0..2]`, `S=datain[1]`; `5 → R4E.Y[1]`, `S=datain[0]` | `T(R35)` / `T(R4E)`, max d1 |
| 13 | `R23/A` | `datain[2]=0` | `1,2,4,5 → R36.Y[1..4]`, `S=datain[1]` | `T(R36)`, max d1 |
| 14 | `R23/B` | `datain[2]=1` | `1,2,4,5 → R37.Y[1..4]`, `S=datain[1]` | `T(R37)`, max d1 |
| 15 | `R30/A` | `datain[1]=0` | `3..5 → R4E.Y[0/1]`, `S=datain[0]` | `T(R4E)`, d0 |
| 16 | `R30/B` | `datain[1]=1` | `2..4 → R4C.Y[0/1]`, `S=datain[0]` | `T(R4C)`, d0 |
| 17 | `R31/A` | `datain[1]=0` | `3..5 → R4C.Y[0/1]`, `S=datain[0]` | `T(R4C)`, d0 |
| 18 | `R31/B` | `datain[1]=1` | `1,2,6 → R4C.Y[0/1]`, `S=datain[0]` | `T(R4C)`, d0 |
| 19 | `R32/A` | `datain[1]=0` | `3,4 → R4C.Y[1]`, `S=datain[0]` | `T(R4C)`, d0 |
| 20 | `R32/B` | `datain[1]=1` | `2,5 → R4C.Y[0/1]`, `S=datain[0]` | `T(R4C)`, d0 |
| 21 | `R33/A` | `datain[1]=0` | `2,5 → R4C.Y[0/1]`, `S=datain[0]` | `T(R4C)`, d0 |
| 22 | `R33/B` | `datain[1]=1` | `1..3 → R4E.Y[0/1]`, `S=datain[0]` | `T(R4E)`, d0 |
| 23 | `R34/A` | `datain[1]=0` | `1,3..5 → R4C.Y[0/1]`, `S=datain[0]` | `T(R4C)`, d0 |
| 24 | `R34/B` | `datain[1]=1` | `2,5 → R4E.Y[0/1]`, `S=datain[0]` | `T(R4E)`, d0 |
| 25 | `R36/A` | `datain[1]=0` | `1..3 → R4C.Y[0/1]`, `S=datain[0]` | `T(R4C)`, d0 |
| 26 | `R36/B` | `datain[1]=1` | `4 → R4E.Y[1]`, `S=datain[0]` | `T(R4E)`, d0 |
| 27 | `R37/A` | `datain[1]=0` | `4 → R4E.Y[1]`, `S=datain[0]` | `T(R4E)`, d0 |
| 28 | `R37/B` | `datain[1]=1` | `2..4 → R4C.Y[0/1]`, `S=datain[0]` | `T(R4C)`, d0 |
| 29 | `$procmux$13/B` | `ena=1` | `1..6 → R00.Y[4..9]`, `S=datain[4]` | `T(R00)`, max d4 |
| 30 | `$procmux$17/B` | `ena=1` | `1 → R00.Y[1]`, `S=datain[4]` | `T(R00)`, max d4 |

位数校验：前两行 14 位，R1 层四行 24 位，R2 层八行 40 位，R3 层十四行 35 位，两个顶层输出入口 7 位，合计 `14+24+40+35+7=120`。

每行的直接原因相同：入口 guard 只固定当前 mux 的选择位或 `ena`，首阻断 mux 的选择是下一低地址位，仍为 UNKNOWN；其两个可达常量叶包含 0 和 1。现有规则既不枚举自由选择，也不保存布尔函数/关系，因此不能继续传播。这里不存在“缺一条常量规则”——目标函数在该 guard 下实际非恒定。

## 6. case3：7 个已证明位的完整证明链

proof ID 是每次入口分析内部的稳定编号，必须与 `source_entry` 一起解释，不是跨入口的全局编号。

| cell / target PMUX 端口 | bit（旧信号） | proof_id | guard | 来源比较器 | 完整规则链 |
|---|---|---:|---|---|---|
| `$procmux$10/B` | 0 (`sr[2]`) | 4 | `$procmux$11_CMP=1` | `$procmux$20_CMP0`: `sr==8'hff` | `1:guard(CMP=1) + 0:constant(1) -> 4:eq_true_constant(sr[2]=1)` |
| `$procmux$10/B` | 1 (`sr[1]`) | 3 | 同上 | 同上 | `1:guard + 0:constant(1) -> 3:eq_true_constant(sr[1]=1)` |
| `$procmux$10/B` | 2 (`sr[0]`) | 2 | 同上 | 同上 | `1:guard + 0:constant(1) -> 2:eq_true_constant(sr[0]=1)` |
| `$procmux$18/B0`，物理 B bit 0 | 0 (`sr[3]`) | 5 | `$procmux$11_CMP=1` | `$procmux$20_CMP0`: `sr==8'hff` | `1:guard + 0:constant(1) -> 5:eq_true_constant(sr[3]=1)` |
| `$procmux$27/B` | 1 (`sr[6]`) | 9 | `$procmux$19_CMP=1` | `$procmux$28_CMP0`: `sr==8'hfe` | `2:guard + 0:constant(0) + 1:constant(1) -> 9:eq_true_constant(sr[6]=1)` |
| `$procmux$27/B` | 2 (`sr[5]`) | 8 | 同上 | 同上 | `2:guard + 0:constant(0) + 1:constant(1) -> 8:eq_true_constant(sr[5]=1)` |
| `$procmux$27/B` | 3 (`sr[4]`) | 7 | 同上 | 同上 | `2:guard + 0:constant(0) + 1:constant(1) -> 7:eq_true_constant(sr[4]=1)` |

### 6.1 这些证明能删除什么

| 结构 | 当前提交实际做的事 | 理论结构结论 | 不能删除的部分 |
|---|---|---|---|
| `$procmux$10`, 3-bit `$mux` | B 三位改为 `3'b111` | guard 为 `sr==ff` 时，A=`{sr[4],sr[5],sr[6]}` 与 B=`{sr[0],sr[1],sr[2]}` 对应位都为 1；整个 mux 可旁路到 A | `$procmux$20_CMP0` 还用于 `$procmux$18` 和写使能逻辑，不能随 mux 删除 |
| `$procmux$18`, 1-bit `$pmux` 的 B0 | B0 改为 1 | B0 与 A 在 `sr==ff` 时同为 1；在证明 `sr==ff` 与 `sr==aa` 互斥后，可删除该 PMUX 分支，并把两分支 PMUX 降为单分支 mux/等价结构 | `sr==ff` 比较器仍有其它扇出；`sr==aa` 分支必须保留 |
| `$procmux$27`, 4-bit `$mux` | B[1:3] 改为 1；B[0] 原本已是 1 | bits 0..2 可分别旁路到 A[0..2]；bit3 在 `sr==fe` 时 A[3]=`sr[0]=0`、B[3]=`sr[4]=1`，必须保留选择 | `$procmux$28_CMP0` 还驱动写使能和 `$sdffce.SRST`；bit3 仍需要选择条件 |

所以七条证明不只是“在数学上只能换数据位”：它们足以作为更强结构规则的前提，能够证明一个完整 mux、一个 PMUX 分支和三个 mux 位函数冗余。但 v0.1 的合法动作只有端口位常量替换；这些删除结论不在当前规则和冻结接口的提交权限内。

### 6.2 没有 LUT 收益的原因

1. `after_pmux.il` 中 `$procmux$10/$18/$27` 仍全部存在，选择端口、Y、宽度和分支数不变。
2. 两个来源比较器都有非选择数据路径的活跃扇出，不会因七个数据位常量化而自然死亡。
3. baseline 和优化网表表示的是同一组布尔函数；ABC9 在 baseline 上也能吸收 guard 与数据的蕴含关系。显式常量化没有降低最终需要的 LUT 数。
4. case3 输出进入 DFF/SDFFCE，映射边界和寄存器数量保持 17；局部真值表变化未跨越这些边界形成额外共享或完整锥删除。

## 7. v0.2 候选规则（仅建议，不实现）

### V2-A：整 cell guarded-arm equivalence

- 输入结构模式：`$mux Y = S ? B : A`，在 `S=1` 的局部 guard 下证明整宽 `A == B`。
- 前置条件：A/B 位宽、位序一致；整宽每位都有可复现关系证明；只穿透组合逻辑；稳定快照仍匹配；首版只接受整 cell，不做拆分。
- 正确性依据：`S=0` 时原输出为 A；`S=1` 时由证明得 B=A，因此所有输入上 `Y=A`。
- 必须拒绝的反例：只证明 B 为常量而没有证明 A 同值；仅部分位相等却删除整 cell；含 x/z、多驱动、不明确扩展/重排；需要跨 DFF 才成立。
- 预计删除的 cell：case3 可删除 `$procmux$10` 这个 3 位 `$mux`；后续 `opt_clean` 只删除真正无扇出的附属逻辑。
- 预计影响 LUT 的原因：去掉完整选择层和它对三位输出的支持变量，可能减少 mux LUT；但 ABC9 可能已做同样布尔吸收，必须以实测为准。
- 是否需要 SAT：case3 不需要，现有 guard/equality 常量传播即可分别证明 A、B 同值；一般化关系可留给 V2-E。
- 对预算的影响：复用每入口 512 cell / 8192 bit / 262144 event；增加另一数据臂的只读提取和逐位关系比较，最坏约两倍锥访问，仍需在同一硬上限内去重计数。

### V2-B：PMUX 冗余分支删除

- 输入结构模式：`$pmux(A, B_0..B_n, S_0..S_n)`，证明在 `S_i=1` 时 `B_i` 等于删除该分支后会被观察到的值。
- 前置条件：整宽相等；对可能同时为真的选择严格保持 RTLIL 优先/不定语义。若依赖互斥，必须从逻辑证明，不信任 `full_case/parallel_case` 属性；快照、切片和 `S_WIDTH` 均匹配。
- 正确性依据：在 `S_i=0` 时删除分支无影响；在 `S_i=1` 时新旧被选值相等；重叠选择由已证明互斥或精确优先条件覆盖。
- 必须拒绝的反例：两个选择可同时为真且删除改变赢家；只在部分位相等；其它分支优先级不清；x/z 选择；用属性代替证明。
- 预计删除的 cell：case3 可删除 `$procmux$18.B0/S0` 切片，把两分支 PMUX 降为单分支 mux；`$procmux$20_CMP0` 因其它扇出仍保留。
- 预计影响 LUT 的原因：减少一个选择输入和一层优先选择逻辑；1 位实例收益可能为 0，但对宽 PMUX 可能减少多颗 LUT。
- 是否需要 SAT：case3 不需要，`sr==ff` 与 `sr==aa` 的常量模式不同即可证明互斥；复杂选择可调用 V2-E。
- 对预算的影响：除当前入口预算外需要受限的选择对互斥检查；建议每 cell 至多检查 64 对，超限保持原样。

### V2-C：带成本门的部分位 mux 旁路

- 输入结构模式：宽 `$mux` 的位集合 K 满足 `S -> (A[K] == B[K])`，但其它位仍需要 mux。
- 前置条件：每位关系证明完整；Y 的消费者和位映射可安全重连；不得跨时序边界；只有预估 cell/LUT 不增加时才允许拆分或按位旁路。
- 正确性依据：K 中每位按 V2-A 的两情形证明恒等于 A；其余位保持原 mux 语义。
- 必须拒绝的反例：位序/扩展不透明；共享 Y 的重连改变其它消费者；拆分新增逻辑多于删除逻辑；未证明的位被一并旁路；x/z 语义变化。
- 预计删除的 cell：case3 `$procmux$27` 可旁路 bits 0..2，只保留 bit3 的 1 位选择函数；比较器、写使能和复位逻辑不删除。
- 预计影响 LUT 的原因：三个位不再包含选择变量，理论上减少宽 mux 的三个 bit-function；是否减少物理 LUT 取决于打包，必须设置 ABC9 实测成本门。
- 是否需要 SAT：case3 不需要；结构传播不能证明的关系可选 V2-E。
- 对预算的影响：证明预算仍按入口；另加每 cell 一次扇出/位切片成本评估。禁止为了实现旁路无界拆分 cell。

### V2-D：小型常量叶 mux/ROM 锥规范化与共享

- 输入结构模式：深度不超过 8、由同一小地址向量控制、叶为确定 0/1 的纯组合 `$mux` 树，例如 case4 的 R00..R4E。
- 前置条件：地址变量数建议不超过 8；无数据型叶、x/z、存储器或时序 cell；按每个输出精确构造 truth table/ROBDD；只有节点数和 LUT 估计严格下降才提交。
- 正确性依据：按 Shannon 展开得到原函数的规范表示；只在完整 truth table 相等时合并同构子树或删除等臂 mux。
- 必须拒绝的反例：case2 那样叶含自由 `data*`；x/z/case 优先语义；超过变量/节点预算；共享重写会增加扇出代价；仅凭“看起来相同”合并位序不同的子树。
- 预计删除的 cell：可能合并或删除 case4 R00..R37 中重复的 `$mux` 子树；本次诊断没有确认一个必删集合，因此只能作为实验候选，不能宣称已有收益。
- 预计影响 LUT 的原因：跨 10 个 ROM 输出共享相同地址子函数，可能比逐输出 mux 树少 LUT；但现有 ABC9 已得到 36 LUT，新增规则很可能没有额外收益。
- 是否需要 SAT：对最多 8 个地址位可穷举 truth table，无需在线 SAT；提交后仍由 EQY 验证。
- 对预算的影响：新增 `2^k × outputs` 求值预算和 BDD/node 上限；建议 `k<=8`、每锥最多 4096 个表项、512 个规范节点，任一超限零改写。

### V2-E：受限 SAT guarded-equivalence 后备

- 输入结构模式：为 V2-A/B/C 的候选构造 `G ∧ (lhs != rhs)`，只在结构传播无法判定时查询。
- 前置条件：纯组合、边界明确、两值语义可精确编码；只接受 UNSAT；查询和冲突数有确定上限；proof/source 记录可追溯。
- 正确性依据：反例公式 UNSAT 等价于 `G -> lhs==rhs`。
- 必须拒绝的反例：SAT、UNKNOWN、超限、求解错误；x/z 编码不等价；锥跨 DFF/存储器/黑盒；多驱动或不清晰符号扩展。
- 预计删除的 cell：SAT 本身不删除 cell，只可能解锁 V2-A/B/C 的 mux、PMUX 分支或位函数删除。
- 预计影响 LUT 的原因：仅间接影响；没有对应结构删除计划时禁止为了“得到更多事实”调用 SAT。
- 是否需要 SAT：是，仅作后备；case3 已知机会不需要 SAT。
- 对预算的影响：必须新增独立的每入口 query 数和 solver conflict/decision 上限，不能用墙钟超时替代确定预算；任一失败保持原样。

## 8. 建议的 v0.2 排序与验收口径

1. 先用 V2-A 验证 case3 `$procmux$10` 是否在 ABC9 后真实降 LUT；这是证据最强、语义最简单的整 cell 候选。
2. 再评估 V2-B 和 V2-C；二者修改范围已经超出冻结的 R1′ 端口位提交接口，必须先更新方案、接口所有权、结构检查和随机测试判据。
3. V2-D 只做离线消融；若不能优于 ABC9 的 36 LUT，不进入默认流程。
4. V2-E 最后立项。SAT 不应掩盖结构规则的成本，也不能改变“失败、超限、UNKNOWN 全部零改写”的回退原则。
5. case1 永久保持寄存器边界；本报告不建议跨寄存器传播。

最终判断：R1′功能实现正确，但未通过评分收益门。下一步应验证“guarded equality 导致真实选择结构删除”能否降低 ABC9 LUT，而不是继续扩大按位常量证明数量。
