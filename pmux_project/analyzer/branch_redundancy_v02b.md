# PMUX v0.2-B：整分支冗余证明接口与语义

状态：A 冻结候选；只读证明，不包含 RTLIL 改写、ABC9 收益判断或主分支集成。

## 1. Yosys 0.69 的 `$pmux` 语义

核对对象固定为 Yosys `9f75ca1f9834a39a863915b5dae0c7b1e33533bc`：

- `docs/source/cell/word_mux.rst` 明确规定 `S=0` 时取 `A`，恰好一个
  `S[n]=1` 时取 `B[n]`，多于一个选择位为 1 时输出未定义。
- `kernel/calc.cc:625-635` 的 `RTLIL::const_pmux` 对非 zero/onehot 的选择
  返回全 `Sx`。
- `techlibs/common/simlib.v:1699-1727` 在第二个活动选择位出现时把 `Y`
  置为 `x`，选择位为 `x` 时也把 `Y` 置为 `x`。
- `kernel/satgen.cc:388-430` 在启用 undef 建模时，把选择 `x` 或多个活动
  选择编码为输出未定义。

`techlibs/common/pmux2mux.v:15-18` 和 SAT 的二值 value plane 从低下标迭代到
高下标，因此其某一种 don’t-care 实现表现为“高下标覆盖低下标”。这不是
原生 `$pmux` 在选择重叠时的语言语义，不能被证明引擎当成优先级保证。

所以原生 `$pmux` 没有“较低优先级分支”。分支 `i` 真正、唯一决定输出的
精确条件是：

```text
effective_guard(i) = S[i] && AND(j != i, !S[j])
```

实现不会把 `AND(j != i, !S[j])` 当作外部假设。它只以 `S[i]=1` 为起始
guard，通过现有三值传播、比较器反推和定点传播逐一证明所有 `S[j]=0`。
任何选择为 1、UNKNOWN、x/z、冲突或超预算都拒绝该证明。

在上述精确 guard 下删除 `i` 后没有其它活动分支，fallback 必然是 `A`。
接口保留 `LowerPriorityBranch` 枚举和值为 `-1` 的
`fallback_branch_index`，供未来另一种明确的 priority-cell 合约使用；本版
对原生 `$pmux` 永不输出 lower-branch fallback。若将来分析显式的优先 MUX
链，高下标优先模型才有
`S[i] && AND(j>i,!S[j])`，fallback 才是活动的最大 `j<i` 分支或 `A`；该模型
不得反向套用到 `$pmux`。

## 2. 冻结接口

公开头文件是 `pmux_project/include/pmux_branch_proof_api.h`。原有
`pmux_opt_api.h` 未修改。

```cpp
struct BranchProof {
    std::string cell;
    int branch_index;
    int width;
    std::vector<GuardFact> effective_guard;
    BranchFallbackKind fallback_kind;
    int fallback_branch_index;
    bool proven_all_bits;
    std::vector<BranchBitProof> per_bit_status;
    std::string reason_code;

    EntryStatus status;
    ResourceUsage usage;
    std::vector<ProofRecord> proof_records;
};
```

`BranchBitProof` 记录 `bit_index`、状态、分支/fallback 三值、双方
`proof_id` 和原因码。B 只能在 `status == Normal && proven_all_bits == true`
时把结果作为“可计划删除整条分支”的证明；逐位结果仅用于诊断，不能授权
部分位或整分支改写。

调用前 B 必须填充并在提交改写前再次核对以下快照字段：

- `module_id`、`cell_id`、`module`、`cell` 及 cell membership；
- cell 类型严格为 `$pmux`；
- `WIDTH`、`S_WIDTH` 和 `branch_index`；
- 完整 `A`、`B`、`S`、`Y` 端口快照；
- 固定预算 `512 cells / 8192 bits / 262144 events`。

分析器只读聚合 `S`、目标 `B[i]` 与 `A` 的组合锥，一次执行共享现有预算；
没有 SAT 查询。结构 alias 或双方被现有证明链推出同一二值时，单个位才为
`ProvenEqual`。首版要求所有位均为 `ProvenEqual`。

## 3. 原因码

| 原因码 | 含义 / B 的动作 |
|---|---|
| `branch_redundant` | 整宽证明完成；仅此结果可交给改写规划器 |
| `partial_or_unknown_equality` | 至少一位未证明；整分支拒绝 |
| `selection_overlap_reachable` | 在 `S[i]=1` 下证明另一选择也为 1；拒绝 |
| `selection_overlap_unknown` | 无法证明另一选择为 0；拒绝 |
| `selection_xz` | 选择含原始 x/z；拒绝 |
| `fact_conflict` | 条件或传播事实冲突；拒绝 |
| `budget_cells_exhausted` | cell 预算耗尽；拒绝 |
| `budget_bits_exhausted` | bit 预算耗尽；拒绝 |
| `budget_events_exhausted` | event 预算耗尽；拒绝 |
| `invalid_*` | 请求、快照、形状、ID 或预算不合法；拒绝 |

逐位原因是 `structural_alias`、`equal_binary_facts`、
`bit_equality_unknown`，或上述 entry 级保守失败原因。UNKNOWN、冲突和预算耗尽
都不会保留 `proven_all_bits=true`。

## 4. 自测覆盖与结果

`branch_selftest_plugin.cc` 覆盖：

1. 整条分支与 `A` 为结构 alias：通过；
2. 目标分支数据等于一个低下标分支、且选择重叠可达：因 `$pmux` 无优先级
   而正确拒绝；
3. 独立选择位的重叠可达性 UNKNOWN：正确拒绝；
4. 只有部分位相等：只保留 per-bit 诊断，整分支拒绝；
5. 数据等价 UNKNOWN：拒绝；
6. 513-cell 组合锥：`budget_cells_exhausted`；
7. 常量 0 选择与 `S[i]=1` 条件冲突：`fact_conflict`；
8. case3 V2-B 的通用比较器形状：通过，未使用单元名或信号名硬编码；
9. 每次调用前后 RTLIL 文本指纹一致。

本机结果：原 v0.1 自测 `2116 assertions` 全部通过；新增分支自测
`29 assertions` 全部通过。

## 5. case3 实际证明记录

对官方冻结 `case3/netlists/stage_proc.il` 全量扫描，不指定单元名。该阶段与
V2-B 对应的结构是 `$procmux$15.branch=1`：`S[1]=(sr==8'hff)`、
`B[1]=sr[3]`、`A=sr[0]`。后续变换规范所称的 `$procmux$18.B0` 是同一
`ff` 分支在不同流水线快照中的编号/位置。

实际输出：

```text
BRANCH_PROOF module=test3 cell=$procmux$15 branch=1 width=1
  proven_all_bits=1 fallback=A reason=branch_redundant
  cells=3 bits=11 events=45
BRANCH_PROOF_BIT bit=0 status=ProvenEqual
  branch_value=1 fallback_value=1
  branch_proof_id=6 fallback_proof_id=3 reason=equal_binary_facts
```

证明链：proof 2 是 `S[1]=1` 的 guard；`$procmux$17_CMP0` 的
`eq_true_constant` 由 proof 2 反推 `sr[7:0]=8'hff`（proof 3..10）；因此
fallback `A=sr[0]=1`（proof 3），目标数据 `B[1]=sr[3]=1`（proof 6）。
同一组事实经 `consteval_forward` 证明其它选择比较器分别为 0（proof 11、
12），从而完成互斥条件。报告最后输出
`BRANCH_PROOF_NETLIST_UNCHANGED 1`。

## 6. 运行命令

```sh
make -C pmux_project/analyzer test
make -C pmux_project/analyzer test-slow
make -C pmux_project/analyzer case3-report
```

`case3-report` 生成忽略文件
`pmux_project/analyzer/case3_branch_proof.log`。诊断命令
`pmux_branch_proof_report` 只遍历并打印证明，不修改设计。
