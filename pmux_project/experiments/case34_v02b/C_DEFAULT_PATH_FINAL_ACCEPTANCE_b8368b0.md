# C 对 b8368b0 的最终默认路径验收

结论：**G3 = PASS**；P0/P1/P2、GENERATION_MONITOR_BINDING、ONE_SHOT_AUTHORIZATION、DEFAULT_PATH、PRODUCTION_CANDIDATES 全部 PASS。所有要求的最终回归完整完成，没有最终 ERROR、UNKNOWN、MISSING 或 UNVERIFIED 项。

## 冻结起点与分支

- C 分支：`feature/c-v02-default-path-final-acceptance-b8368b0`。
- 本报告所在的 C 完整提交用 `git rev-parse HEAD` 获取，并列于交付目录 `DELIVERY_VERIFICATION.json`（完整提交不能嵌入自身被哈希内容）。
- B：`b8368b0f3e2d55a2bb429296a3aea859eb4f2049`；直接父提交：`ec86209a61f22020707b299f5175f02b8d71495d`。
- A 授权：`d03381f4511ab520372f3c2e57657b141a4959bc`，分支 `feature/a-v02-generation-monitor-reaudit-b8368b0`，报告 `pmux_project/analyzer/generation_monitor_reaudit_b8368b0.md`，AUTHORIZED。A 报告仅以只读证据复制，无合并。
- 所有工具、输入、运行环境及实际可执行文件 SHA-256 在 `c_b8368b0_evidence/frozen_provenance.json`。固定 Yosys/EQY/SBY/ABC 源码提交均逐项核对。
- 独立 worktree 从精确 B 建立。全部 gate/EQY/日志从本轮不存在的输出目录生成；旧 C 驱动仅作代码模板，不复用旧结果。

## 完整回归

19/19 真实微测试 PASS；production mutation seam 26/26 PASS。Analyzer fast 的五组 assertion 数为 2116/29/60/50/81，slow 为 2118/29/60/50/81，全部 PASS。

默认模式固定种子 **1000/1000 PASS**；显式 `-v02-case34` 固定种子 **1000/1000 PASS**。两组独立目录均使用冻结的 1000 个唯一 seed，清单 SHA-256 `8db91b2ee25d579493dbc2ca66417cc945e215b5424349884013834d43df7ac4`；每项 structure、真实 EQY 和 synthesis PASS。逐项原始结果在 `reports/c-b8368b0-default-final/random_summary.json`，逐 seed 命令、gate 和 EQY work/PASS 在 random 子目录。冻结随机检查器与验收 oracle 未修改。

case3 decision/production 和 case4 production 的只读报告前后 RTLIL SHA-256 相同，详见 core_results.json。case4 全部 32×10 位逐行与官方 RTL 核对，地址 0 为 `1001110000`；CSV 保存两侧位串。P1 生产源码扫描和人工分类见 p1_identity_review.md、p1_identity_census.json。

## 官方四例

| case | baseline $lut/DFF | 最终 $lut/DFF | IO 增量 | profile | whole-design EQY |
|---|---:|---:|---:|---|---|
| 1 | 288/222 | 288/222 | 0 | stock | PASS |
| 2 | 25/0 | 25/0 | 0 | stock | PASS |
| 3 | 13/17 | 11/12 | 0 | stock | PASS |
| 4 | 36/10 | 27/10 | 0 | accepted_constant_table | PASS |

四例 gold 均为本轮复制并比对哈希的官方 RTL。真实 source-loaded `synth_intel` 默认路径生成 post-ABC9 gate、JSON，随后完成 map_cells 后保存最终网表。oracle 的资源统计和 whole-design EQY 使用 post-ABC9 gate；完整后续映射网表另存 final.il。EQY 采用冻结配置、`[collect] merge *`，四个工作目录均有 PASS。baseline 仅使用同一个真实源加载入口加 `-no-pmux-v02`。无 B/A 已生成 gate 参与。

统计 JSON/CSV 同时保存 IO、总 cell、结构改写数、CPU/ABC 时间和峰值内存。case1/2 无结构改写；case3/4 各 1，分别 stock 和 accepted_constant_table。

## 生命周期与消费者一致性

**58/58 PASS**，即 native source-loaded synth_intel 与 pmux_opt_abc9 runner 各 29 个夹具。C 拦截真实生产 pass 的执行边界，调用未改动生产逻辑；只在合法发布后修改被测状态。runner 的扰动在通用 techmap/opt 完成后、授权消费者前实施，native 在 producer 后实施。每个夹具保留阶段 RTLIL、真实日志、运行环境与断言。

| 模式 | 两条消费者相同 profile | 相同拒绝原因 | native / runner |
|---|---|---|---|
| none | accepted_constant_table | — | PASS / PASS |
| forge | stock | target_lifecycle_mismatch | PASS / PASS |
| forge_suffix | stock | target_lifecycle_mismatch | PASS / PASS |
| forge_numeric | stock | target_lifecycle_mismatch | PASS / PASS |
| empty_generation | stock | target_lifecycle_mismatch | PASS / PASS |
| lifecycle_false | stock | target_lifecycle_mismatch | PASS / PASS |
| old_generation | stock | target_lifecycle_mismatch | PASS / PASS |
| foreign_generation | stock | target_lifecycle_mismatch | PASS / PASS |
| unregister | stock | target_lifecycle_mismatch | PASS / PASS |
| second_generation | accepted_constant_table | — | PASS / PASS |
| same_name_rebuild | stock | target_lifecycle_mismatch | PASS / PASS |
| module_rebuild | stock | target_lifecycle_mismatch | PASS / PASS |
| port_restore | stock | target_lifecycle_mismatch | PASS / PASS |
| allocator_reuse | stock | target_lifecycle_mismatch | PASS / PASS |
| lut_bit | stock | target_snapshot_mismatch | PASS / PASS |
| lut_width | stock | target_snapshot_mismatch | PASS / PASS |
| attribute | stock | target_snapshot_mismatch | PASS / PASS |
| type | stock | target_snapshot_mismatch | PASS / PASS |
| port_order | stock | target_lifecycle_mismatch | PASS / PASS |
| remove_one | stock | target_lifecycle_mismatch | PASS / PASS |
| remove_all | stock | target_lifecycle_mismatch | PASS / PASS |
| add_extra | stock | target_snapshot_mismatch | PASS / PASS |
| drop_snapshot | stock | target_snapshot_mismatch | PASS / PASS |
| auth_false | stock | — | PASS / PASS |
| unknown_auth | stock | — | PASS / PASS |
| stub_unknown | stock | — | PASS / PASS |
| disable | stock | — | PASS / PASS |
| replay_no_auth | accepted_constant_table → stock | — | PASS / PASS |
| replay_old_auth | accepted_constant_table → stock | target_lifecycle_mismatch | PASS / PASS |

forged 的三种不同非空字符串、旧代和另一真实 Design 的 generation，均观察到 lifecycle_valid=1、lifecycle_monitor_match=0、profile=stock、reason=target_lifecycle_mismatch。另一 Design 的 token 由真正发布操作生成，其在所属 Design 上 monitor_match=1。两次连续发布只可消费当前代。

同名 cell 完整重建、同名 module 重建以及端口改变后恢复，payload_equal=1 且 lifecycle_valid=0，均拒绝；allocator_reuse 在两条路径实际观察到 pointer_reused=1 后仍拒绝（该项环境 GLIBC_TUNABLES=glibc.malloc.tcache_count=0）。首次消费后完整重生目标 cell，无授权或强制回放旧四字段均 stock。

所有成功及拒绝路径均通过真实 techmap/ABC9 执行前的四字段清空断言：authorization=false、snapshot=空、generation=空、lifecycle=false。最终逐项比较两条消费者所有 profile 和 reason；29/29 一致。

## 判定与边界

| 判定 | 结果 | 独立证据 |
|---|---|---|
| P0 | PASS | 19 微测试、两组 1000 种子、四例 whole-design EQY |
| P1 | PASS | 生产源码身份扫描、人工检查、结构候选与快照身份用途分类 |
| P2 | PASS | 四例严格 LUT/DFF/IO oracle |
| GENERATION_MONITOR_BINDING | PASS | 真实 forged/旧代/跨 Design/注销/重建/地址复用拒绝 |
| ONE_SHOT_AUTHORIZATION | PASS | 副作用前四字段清零、完整重生和强制旧授权回放拒绝 |
| DEFAULT_PATH | PASS | 精确 B 的 source-loaded synth_intel；29/29 runner 语义一致 |
| PRODUCTION_CANDIDATES | PASS | case3/4 结构改写、资源达标、官方 gold EQY PASS |
| G3 | PASS | 所有必需 gate 完整满足 |

## 所有调试差异、ERROR 与覆盖修正

以下是 C 测试夹具的准备阶段记录，不计入最终 58 项；原始输出单独保留，未用生产修改或 oracle 放宽消除失败。

1. C 编译时 unique_ptr 的 log_assert 参数改为显式 !=nullptr；未改生产代码。
2. 初始 allocator 实验虽然拒绝正确，但未实际复用地址，覆盖断言失败。因此未写 PASS，归档 lifecycle_setup_attempt_no_address_reuse。关闭 glibc tcache 并用普通 Design API 重建多个实际目标，最终两个消费者均必须观察到地址复用。后续布局变化再次使单目标覆盖不足，归档 lifecycle_setup_attempt_allocator_context 后扩大被重建对象范围；拒绝断言不变。
3. 尝试直接使用受保护 Cell 构造/添加 API 编译失败，已放弃。该次独立编译输出没有另存完整日志（准备阶段 MISSING），源代码调整过程由 C 脚本保留；最终已归档成功编译日志。此试验没有生成候选验收结果。
4. 初始 replay 夹具保存独立 Module clone，Yosys IdString GC 未追踪它，第一次映射写后端发生 unordered_map::at 异常（rc=-6），尚未第二次消费。归档 lifecycle_setup_attempt_unowned_clone；改用 Design 持有 clone 后通过。
5. 初始额外 type 变异在 runner 通用映射前注入，导致其到达消费者的状态与 native 不同：两条路径都 stock，但 snapshot/lifecycle 拒绝原因不同。归档 lifecycle_setup_attempt_mixed_boundary 全部原始行，调整 C 扰动时机到相同消费者边界后逐项原因一致。

6. 新增交付脚本后重建哈希清单时，一次校验启动早于 RAW_INVENTORY 写完，读取旧 SHA256SUMS 与写入中的 inventory 不一致；未提交这份中间状态。等归档写入完成后重新校验及 ZIP 逐项 SHA 校验均通过。

最终候选验收无 ERROR/UNKNOWN/MISSING/UNVERIFIED；准备阶段的上述编译失败、覆盖失败、后端异常及未单独保存的编译输出均已明确披露，不能当作 EQY 不等价或最终 PASS 项。

## 修改文件与工作树

仅新增本报告及 `c_b8368b0_evidence/` 的 C 驱动、夹具、摘要、报告和清单；完整文件列表见 FILES_CHANGED.txt。生产 pass、techlibs、冻结接口、Analyzer、官方 RTL、测试与 oracle 的 diff 为零。构建 .so 和完整原始输出在 ignored reports 目录并进入完整证据 ZIP。

提交和推送完成后的完整 HEAD、远端 HEAD、工作树状态及 ZIP SHA-256 记录于交付目录 DELIVERY_VERIFICATION.json。仅推送 C 分支，不合并主分支。MANIFEST.md 解释原始输出位置、完整性清单和复核命令。
