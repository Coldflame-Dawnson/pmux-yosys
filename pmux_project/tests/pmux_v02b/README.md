# PMUX v0.2-B：C 独立验证矩阵

本目录的 22 个最小 RTLIL 用例覆盖题定的 1–19 类；第 15 类深度、cell、bit、event 预算分别独立成例。第 20 类 case3 V2-B 使用上一阶段冻结的 B 结构消融产物，由 C 的 case3 矩阵脚本重新执行。总共 23 个定向条目。

所有 micro before/after、plan/stats 由 C 预制，模式固定为 FIXTURE。plan 中的 removed_branches 和 result_type、stats 中的 reason_codes 使用 fixture 命名空间，绝不是 A/B 的机器接口。正例结构检查核对被删分支在 B/S 的精确物理切片、S_WIDTH、PMUX→MUX、最后一个分支删除时 Y=A、非目标 cell 与新增 cell。拒绝例要求顶层 JSON 完全不变；快照失配及非法 branch index 要求计划为空、changed_cells=0。每例随后运行 Yosys check、EQY、ABC9 前后综合，并保存 CPU/峰值内存。

预算四例目前使用 budget.forced=true 强制回退，检验的是 C 框架的拒绝与记录路径，尚不能检验 A 的真实预算实现。UNKNOWN、冲突、keep、selection 与原子回退也同样不能从 fixture PASS 推断真实 Pass 正确。

A 的 v0.2-B 接口说明确认：原生 `$pmux` 采用 one-hot 语义，整分支删除后的 fallback 只能是 A。第 9 类展示分支数据与另一分支相同且选择可能重叠的情形，预期拒绝删除；不能把另一分支当作低优先级 fallback。该用例使用 `fixture_non_a_fallback_reject` 本地原因类别，真实 Pass 原因码仍待 A/B 机器契约核对。A 的证明提交 `3aff789a3badf6fecc4d3651e4b7eea52627fe3c` 已推送并在隔离检出中独立运行自测和 case3 只读报告；详见 reports/c-v02b-a-proof-audit。它尚未集成到 B 的生产 Pass，不能计作真实改写、EQY 或 LUT 收益。

A 的冻结原因码包含 `branch_redundant`、`partial_or_unknown_equality`、`selection_overlap_reachable`、`selection_overlap_unknown`、`selection_xz`、`fact_conflict`、cell/bit/event 预算耗尽及 `invalid_*`。文档未给出独立的深度预算原因码；第 15a 类真实路径如何编码须由 B 的机器契约明确，C 不自行映射。

随机种子沿用 M1 的 seeds_1000.txt。前 100 种子中偶数为互斥选择的可删分支，奇数为可能重叠选择的拒绝例；每种子生成前后 RTLIL、计划、统计、命令和完整日志。失败时单独保存 failure.json，包含种子、哈希、版本和一条命令复现。1000 例是 A+B 真实路径的阶段门槛，目前 NOT_RUN。

## 已执行命令

在仓库根目录：

    python3 pmux_project/scripts/generate_v02b_cases.py
    python3 pmux_project/scripts/run_v02b_fixture.py --suite micro --bin /home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/bin --out pmux_project/reports/c-v02b-micro-release
    python3 pmux_project/scripts/run_v02b_fixture.py --suite random --count 100 --bin /home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/bin --out pmux_project/reports/c-v02b-random100-release
    python3 pmux_project/scripts/run_case3_variant_matrix.py --bin /home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/bin --source /home/yuewen/work/pmux-yosys/pmux_project/pmux_case/competition_case/test3.v --manifest pmux_project/tests/case3_variants/manifest.json --out pmux_project/reports/c-v02b-case3-fixture
    python3 pmux_project/scripts/run_official.py --mode identity --bin /home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/bin --source-root /home/yuewen/work/pmux-yosys/pmux_project/pmux_case/competition_case --out pmux_project/reports/c-v02b-official-baseline
    python3 pmux_project/scripts/summarize_v02b_gate.py --micro pmux_project/reports/c-v02b-micro-release/summary.json --random100 pmux_project/reports/c-v02b-random100-release/summary.json --case3-fixture pmux_project/reports/c-v02b-case3-fixture/summary.json --official-baseline pmux_project/reports/c-v02b-official-baseline/summary.json --a-proof-audit pmux_project/reports/c-v02b-a-proof-audit/summary.json --out pmux_project/reports/c-v02b-stage-gate-release

gate 汇总由 scripts/summarize_v02b_gate.py 生成。所有上述运行使用独立输出目录；重跑时选择新的输出目录以保留失败证据。

## 真实接口切换

A/B 尚未交付 v0.2-B 生产提交、Pass 插入方式、机器 plan/stats 字段和原因码。C 在收到完整提交哈希与接口契约后接入真实 Pass，保持本目录的输入设计、期望分支索引及结构语义不变。真实报告必须单独标记 REAL_PASS，记录 A/B 完整哈希和插件哈希；不得把此目录的 fixture 结果或冻结 case3 手工结构消融记为真实生产收益。

真实阶段须逐例验证原子失败零写入、实际物理 B/S 分支、EQY、Yosys check、综合资源、原因码和计划统计。官方流程继续要求 Pass 在 opt -undriven -fine 后、common techmap 前恰好执行一次，且 baseline 无插件。新 Pass 必须使 case3 真实路径 13→12 LUT，另外三案无 EQY、结构或资源回退。全部真实门槛满足前，ready_to_merge_integration_g3 始终为 false，不合入 integration/g3。
