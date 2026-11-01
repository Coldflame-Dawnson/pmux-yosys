# PMUX 优化技术方案 v0.2

## 当前版本状态与执行索引（2026-11-01 文档收口提案）

本节与[统一默认路径交付状态](统一默认路径交付状态_2026-11-01.md)为当前状态；后面的第 0～12 节保留为**历史方案正文**，其中待审、缺件和实验状态描述不能覆盖本节。完整旧版另见[收口前快照](archive/PMUX优化技术方案_v0.2_收口前历史快照.md)。历史算法来源和被拒绝反例继续保留，不伪造当时已完成验收。

- 生产对象：B `dbb21ecd5d50d5636aec7415d9a8e6adb9b5f300`。
- A 独立授权与验收：`feb137e459a329e01ff3ee87d3e3acb04a942d72`，结论 `AUTHORIZED`；[精确报告](https://github.com/Coldflame-Dawnson/pmux-yosys/blob/feb137e459a329e01ff3ee87d3e3acb04a942d72/pmux_project/experiments/case12_v02/a_unified_default_audit/AUDIT_REPORT.md)。授权不自动继承本次仅文档后继提交，须 A 检查零生产 diff。
- 当前职责以[G3双人契约](G3双人协作与接口契约.md)为准：B 四族生产实现和集成；A 证明、独立审计、最终验收；ABC9 为映射工具。
- 本轮只补文档入库，不更改代码、官方 RTL、冻结接口或 oracle，不重跑长实验、不合并主/集成分支、不由 B 宣告 G3/release。

| case | baseline `$lut`/DFF | 默认 post-ABC9 `$lut` | 同阶段 `cycloneiv_lcell_comb` | DFF | profile | whole-design EQY |
|---|---|---:|---:|---:|---|---|
| 1 | 288/222 | 182 | 0 | 222 | stock | PASS |
| 2 | 25/0 | 0 | 15 | 0 | stock | PASS |
| 3 | 13/17 | 11 | 0 | 12 | stock | PASS |
| 4 | 36/10 | 27 | 0 | 10 | accepted_constant_table | PASS |

以上是 A 独立重生成并验收的最好已知生产结果，不是全局最优。四例 IO cell 增量为 0。case2 的 15 为厂商原语，不是 15 `$lut`；LUT6=10 只是参考模型。case4 厂商映射后为 **26 个原语加 1 个 `$not`**，不能充作 post-ABC9 26 `$lut`。历史 26 `$lut` 获胜网表/配置/PASS/哈希仍 `MISSING/UNVERIFIED`，错误重建表仍拒绝；本轮不下调原 P2 目标，A 应将当前生产门禁与完整历史目标闭合分别判定。

### 当前 5.1 步骤的实现状态与双人分工

仍严格执行 `P0 > P1 > P2 > P3`；运行时只按结构，验收表不进入分派。

| 步骤 | 可执行动作、边界和顺序 | B 任务 | A 任务与已验状态 |
|---|---|---|---|
| S0 输入冻结 | 精确提交、官方哈希、冻结工具、干净工作树 | 提供对象及可移植材料 | 独立复核，已验 |
| S1 结构分析 | SigMap、驱动/扇出、参数/实端口宽度、选择/属性/时序边界 | 统一入口和结构索引 | 宽度及边界正反例，已验 |
| S2 特征提取 | 共享选择双PMUX、四分支优先链、guard决策锥、有界常量表 | 仅依据拓扑和端口匹配 | 无身份硬编码，已验 |
| S3 规则/候选 | 默认 case12 后 case34 再通用分析；冲突和保护保守拒绝 | 固定调度、计划和快照 | 20/20 默认/显式/回退/冲突/系列/泄漏，已验 |
| S4 证明 | 现有三值/比较反推/定点和结构义务；预算不放宽、不无条件SAT、不跨寄存器 | 只消费完整证明 | fast/slow、consumer/order与预算，已验 |
| S5 原子改写 | 提交前重检全参数/端口/位序/扇出/属性，失败零写入，随后opt_clean | 唯一生产写入者 | 宽度8/8、结构15/15、扇出4/4、重叠256/256，已验 |
| S6 成本门 | 完整ABC9与厂商双口径、DFF/IO/CPU/ABC/RSS；profile一次性消费 | 生产映射选择、全局及单族回退 | 四例、26/26强化生命周期，已验 |
| S7 EQY门 | 官方gold对完整gate，对称whole-design配置，不增假设/删端口/减比较点 | 提供可复现驱动，不自批 | 四例PASS、19微测试、默认与显式各1000/1000，已验 |
| S8 选择/接受 | 接受经独立验证的精确生产对象，失败退回已验版本 | 交候选和证据 | dbb21ec已授权；完整P2历史26仍需闭合 |
| S9 集成/发布 | 机械集成后最终diff检查及必要短回归 | 等批准后集成，不顺手改代码 | 当前未合并/发布，需A明确最终结论 |

**实现边界：** Pass 内做证明、快照与原子改写；整设计 EQY、完整映射成本和版本接受在离线验收层。生产 Pass 不会在每次改写时自动运行整设计 EQY；全候选通用搜索和逐设计端到端成本事务没有因流程图而自动实现。后文的理想流程应按此边界理解。

**生产限制：** priority 规则为合格的真正最外层**四分支**链，不是任意链长；五分支取内部四层必须拒绝。宽度不按官方5位身份特判。配对译码先核验 `$eq` 的 A/B/Y 参数与实端口一致，整批畸形宽度零写入。所有消费者被覆盖或明确保留，不能只记录无序support。case4 的授权绑定内容、目标集合、生命周期和实际monitor generation，副作用前一次性清零。冻结接口和证明实现未在本轮修改。

**本次变更记录：** 来源是 A 精确审计报告及其 `MANIFEST.md/SHA256SUMS/commands.txt/toolchain.txt`；影响范围仅文档、状态和交接；不影响 EQY关系或四例目标，不更改验收期望。本文补入远端缺失的设计文档和双人契约，待A复核，不把文档后继当作新授权。

---

## 历史方案正文（保留来源、算法细节与当时缺口；当前状态见上文）

## 0. 文档状态、范围与证据约定

**状态：方案持续维护；2026-10-24 起切换为 A/B 双人制。** 本版把 v0.1 的局部数据位常量化扩展为一个结构驱动的规则框架，并把四份独立实验的已知结果转化为统一算法的验收目标。算法语义和 P0～P3 不因人数变化而改变；当前人员职责、文件所有权、交接与批准流程以 [G3 双人契约](G3双人协作与接口契约.md)和 [M0 双人切换清单](M0双人协作切换检查清单.md)为准。原 [G3 三人契约](G3三人并行分工与接口契约.md)和 [M0 三人清单](M0三人协作启动检查清单.md)仅保留历史追溯价值。[v0.1](PMUX优化技术方案_v0.1.md)、[工程状态](工程状态.md)。

本版依据的资料按要求依次核阅：`design/工程状态.md`、`design/PMUX优化技术方案_v0.1.md`、`design/G3三人并行分工与接口契约.md`、`design/M0三人协作启动检查清单.md`、`case_1/CASE1_182LUT_特别详细方法说明.md`、`case2_lut_opt_workspace/OPTIMIZATION_ALGORITHM_AND_REPRODUCTION.md`、`case_3_4/RESULTS.md`。随后 A 完成 case2 短流程补证；2026-10-07 又只读核阅 `case_1/submit/` 和 `case_3_4/submit/submit/` 的新增附件，包括说明、算法脚本、阶段网表/JSON、日志、EQY 配置及清单。后者实际有两层 `submit`，本文按实际路径引用，不移动附件。四份官方 RTL 位于 `competition_case/test1.v`～`test4.v`，SHA-256 与 [M0 清单第 6 节](M0三人协作启动检查清单.md)一致。本次附件核阅只做文件哈希、顶层 JSON 计数、连接/日志和真值表静态核对，没有重新综合、运行 EQY 或长搜索。[case2 补证结果](../case2_lut_opt_workspace/evidence_case2/RESULTS.md)、[case1 清单](../case_1/submit/MANIFEST.md)、[case3/4 清单](../case_3_4/submit/submit/MANIFEST.md)。

**2026-10-28 实施状态补记：** case3/4 的默认生产路径已经在 B 提交 `b8368b0f3e2d55a2bb429296a3aea859eb4f2049` 上完成原 A 授权和原 C 独立验收，历史验收提交为 `23ef422c7e5302f9eb96175cd315179672aa0ca9`；case3 为 11 LUT/12 DFF，case4 为 27 LUT/10 DFF。case1/2 的生产候选 `504df2a389bf20d09e3bdaae64528caacc0c7bcf` 已推送 `origin/feature/b-v02-case12-production`；B 本地自测观察到 case1 182 LUT/222 DFF、case2 15 个 `cycloneiv_lcell_comb`。A 尚未完成独立审计和验收，因此状态仍为 `PENDING_A_AUDIT/UNVERIFIED`。

证据等级在本文统一使用：

| 标记 | 精确定义 |
|---|---|
| 已证明最优 | 在明确的模型和候选类中有可检查下界，且构造达到下界；不得省略模型范围。 |
| 最好已知结果 | 有已记录的数值、候选及验证陈述，但未证明全局最小。 |
| UNVERIFIED | 有报告称成功，但当前资料包缺少对应输入、网表、配置、日志或哈希闭环，不能独立复核该具体结论。 |
| MISSING | 要求的文件、版本、命令或实现未在指定目录找到。 |
| RECREATED | 附件声明为依据旧记录重建的材料；描述来源，不代表已运行、与原产物一致或通过 EQY。 |
| REFUTED / P0 拒绝 | 当前候选存在可检查的语义反例；即使带有目标 LUT 数字，也不得作为等价候选或达标证据。 |

“最好已知结果”和“UNVERIFIED”可同时成立：前者描述观察到的候选排序，后者描述本资料包的证据完整性。数值只在明确标出的统计口径内比较，不把 `$lut`、LUT6 和 `cycloneiv_lcell_comb` 混为一个指标。[case1 方法](../case_1/CASE1_182LUT_特别详细方法说明.md)、[case2 方法](../case2_lut_opt_workspace/OPTIMIZATION_ALGORITHM_AND_REPRODUCTION.md)、[case3/4 结果](../case_3_4/RESULTS.md)。

### 0.1 相对 v0.1 的目标变化

v0.1 的 R1′只在局部 guard 下把当前 `$mux/$pmux` 的 `A/B` 数据端口位替换为已证明的二值常量，不新增 cell，也不保证 ABC9 后 LUT 下降。[v0.1 §0.5、§5](PMUX优化技术方案_v0.1.md)。[工程状态 §8](工程状态.md)报告旧生产 Pass 在官方四例分别为 `288/25/13/36 → 288/25/13/36`，均通过 EQY，但没有 LUT 收益。v0.2 的目标是让同一个运行入口识别四类结构机会，经过证明、成本和 EQY 门后选择候选；四例的单独实验结果是验收目标，不是生产实现现状。

## 1. 不可交换的优先级与核心矛盾

执行顺序严格为 **P0 > P1 > P2 > P3**。任何低优先级目标与高优先级目标冲突时，保留高优先级目标和上一版已验证网表，记录拒绝原因。

| 优先级 | 约束 | 运行与验收动作 |
|---|---|---|
| P0：EQY 结果保持 PASS | 四例各自必须完成 whole-design EQY；gold 为官方 RTL，gate 为对应优化网表；新规则不能替代无法证明等价的旧规则。 | 保存实际 gold/gate、EQY 配置、完整日志、PASS 标记和哈希；任一失败或未完成，候选拒绝。缺件标 `MISSING/UNVERIFIED`。 |
| P1：统一完整算法 | 禁止按 case 身份分派；允许依据可验证的结构条件特化规则。 | 一个入口、一套分析/规则库/证明/改写/成本/EQY/回退流程。运行时不得读取文件名、模块名、top 名、case 编号或源码位置作为规则条件。 |
| P2：四例达到各自最好已知且已验证的结果 | 四个官方样例是回归 oracle，不是运行时查表。 | 在相同统计口径和正式流程下逐例比较；目标证据不足或口径不一致时不得假称已通过 P2。 |
| P3：整体算法优化 | 抽象共享规则、减少运行代价、优化 ABC9 脚本和映射。 | 只有 P0、P1、P2 均成立且四例不回退时，默认启用新的成本或映射策略。 |

P1 与 P2 并不矛盾：把四例中观察到的结构关系抽象为规则族，由电路拓扑、端口、真值表和 guard 触发；在独立验收脚本中记录四例的 LUT/EQY oracle。若某实验的收益只能靠“这是 caseN”或固定名字识别，或必须把该 case 的特定向量写进运行时，它尚不能进入 P1 合法的生产规则，P2 对该项仍为未达成。禁止四个 case 专用脚本由外层包装器按名称分派。历史 case1 原型按已知自动名定位，仅能作为算法来源；case3/4 后续已经形成结构驱动生产实现并完成历史验收，case1/2 新生产候选仍需 A 独立确认其结构匹配不含身份信息。[case1 方法 §6、§11](../case_1/CASE1_182LUT_特别详细方法说明.md)、[case3/4 结果 §5–9](../case_3_4/RESULTS.md)。

## 2. 资料清点、可追溯路径和缺口

下表的“存在”指本次交付目录 `D:/Work/Embedded_competition/Project_1` 中实际可读；文献中的旧 `E:/...`、`/tmp/...` 或 `pmux_project/...` 路径不能自动视为此目录存在。

| case | 实际读到的材料及路径 | 文档引用但当前未找到的关键文件 | 证据结论 |
|---|---|---|---|
| 1 | [清单](../case_1/submit/MANIFEST.md)、[结果](../case_1/submit/RESULTS.md)、[原型](../case_1/submit/prototype_rtlil.py)、[结构审计](../case_1/submit/structure/structure_audit.md)、[来源](../case_1/submit/PROVENANCE.md)、[证据链](../case_1/submit/CHAIN_OF_CUSTODY.md)、[阶段 JSON](../case_1/submit/json/post_abc9_candidate.json)、[EQY 配置](../case_1/submit/eqy/rtlil_prototype.eqy)、[完整日志](../case_1/submit/eqy/rtlil_prototype.eqy_work/logfile.txt)、[PASS](../case_1/submit/eqy/rtlil_prototype.eqy_work/PASS)、[哈希清单](../case_1/submit/SHA256SUMS)；`stages/`、`search/`、`logs/` 和 `RESOURCE_AND_TIMING.md` 均存在。 | 原实验 ABC/EQY/SBY 的完整提交与二进制哈希、通用 C++ Pass、新规则四例回归：`MISSING`；重打包后旧绝对路径及 Python 导入布局的可移植重放：`UNVERIFIED`。 | 322 项附件哈希通过；独立顶层 JSON 复核 288→182 `$lut`、286→180 原语、222 DFF 不变；归档 whole-design EQY PASS 可追溯。是已验证归档的最好已知结果，不是生产实现或全局最优。 |
| 2 | [算法说明](../case2_lut_opt_workspace/OPTIMIZATION_ALGORITHM_AND_REPRODUCTION.md)、[原生成器](../case2_lut_opt_workspace/cycloneiv_opt.py)、[补证清单](../case2_lut_opt_workspace/evidence_case2/MANIFEST.md)、[阶段计数](../case2_lut_opt_workspace/evidence_case2/stage_counts.csv)、[工具提交](../case2_lut_opt_workspace/evidence_case2/toolchain.txt)、[SHA256SUMS](../case2_lut_opt_workspace/evidence_case2/SHA256SUMS)；含 ABC9 前后及 `map_cells` 后的 IL/JSON、两组 EQY 配置/工作目录、原网表和脚本副本，未复制官方 RTL。 | 统一生产 Pass 直接生成的最终 gate、对应 EQY 和四例共同回归：`MISSING`；15 原语的一般多输出全局下界：`MISSING`。 | 补证实测：行为级候选 post-ABC9 为 25 `$lut`，显式原语候选为 0 `$lut` + 15 `cycloneiv_lcell_comb`；两条候选 EQY PASS；仍不能宣称统一生产算法达标。 |
| 3 | 历史材料仍见 [包状态](../case_3_4/submit/submit/STATUS.md)和 [guard 说明](../case_3_4/submit/submit/case3/proof/CASE3_PROOF.md)；正式生产证据绑定 B `b8368b0...` 与历史 C 验收 `23ef422...`。 | 历史重建包缺原产物的事实不变；后续生产实现另行补齐了结构证明、完整 gate、EQY 和回归。 | 当前已验收生产结果为 11 LUT/12 DFF、whole-design EQY PASS；新提交仍须 A 重验。 |
| 4 | 历史错误表和 26 LUT 重建候选仍按 [语义说明](../case_3_4/submit/submit/case4/proof/CASE4_SEMANTICS.md)保留为反例；正式生产证据绑定 B `b8368b0...` 与历史 C 验收 `23ef422...`。 | 原 26 LUT 实验产物仍缺，且错误表候选继续 `REFUTED`；生产规则使用重新核对的 32 行表和生命周期授权。 | 当前已验收生产结果为 27 LUT/10 DFF、whole-design EQY PASS；不得把未验证的 26 写成当前生产结果。 |

case2 的新补证中，显式候选和行为级 post-ABC9 候选均以官方 `test2.v` 为 gold，分别有独立的 [显式候选 PASS](../case2_lut_opt_workspace/evidence_case2/eqy_explicit/PASS) 和 [行为级候选 PASS](../case2_lut_opt_workspace/evidence_case2/eqy_behavioral_post_abc9/PASS)。这两份证据证明的是两个**实验候选网表**；统一 Pass 尚未产生可供 EQY 检验的正式最终 gate。[case2 补证结果](../case2_lut_opt_workspace/evidence_case2/RESULTS.md)。

### 2.1 新增附件的只读复核记录（2026-10-07）

| 核验项 | 实际结果 | 路径、证据等级及尚未闭合的边界 |
|---|---|---|
| case1 文件完整性 | 323 个文件，SHA256SUMS 覆盖除自身外的 322 个文件；322/322 匹配，无缺件或未列文件。清单自身 SHA-256 为 `7D510C376A73A9130BACC6AB989F094DA241776A1F62C852B1FE3166F4FAEF0F`。 | [SHA256SUMS](../case_1/submit/SHA256SUMS)、[FILE_INDEX](../case_1/submit/FILE_INDEX.csv)。哈希验证完整性，不独立证明生成者身份。 |
| case1 输入/证明对象 | `competition_case/test1.v`、`inputs/official_test1.v`、`eqy/gold/test1.v` 的 SHA-256 均为 `2D2DBA011B0E8799D0DE3790B69B7F01C6EB6AC665C4321B57B0378A4972FD13`；实际 gate `eqy/gate/rtlil_prototype.top.il` 为 `ADABB9876BA2219DE2E69D51689AAA09B90BA82478FC46B4B40D31CF068B65B3`。 | [证据链](../case_1/submit/CHAIN_OF_CUSTODY.md)、[顶层提取审计](../case_1/submit/GATE_EXTRACTION_CHECK.md)。本次复核哈希/归档关联，未重新执行提取或 EQY。 |
| case1 阶段计数 | 指定 `modules.test1.cells`：ABC9 后 288→182 `$lut`、222→222 `dffeas`、总 cell 510→404；厂商映射后 286→180 `cycloneiv_lcell_comb`、2→2 `$not`、222→222 `dffeas`、总 cell 510→404；IO pad cell 0。 | [ABC9 基线](../case_1/submit/json/post_abc9_baseline.json)、[ABC9 候选](../case_1/submit/json/post_abc9_candidate.json)、[厂商基线](../case_1/submit/json/vendor_baseline.json)、[厂商候选](../case_1/submit/json/vendor_candidate.json)、[资源](../case_1/submit/RESOURCE_AND_TIMING.md)。最好已知归档结果，不是全局最优。 |
| case1 拓扑与时序边界 | 插入点原 3 个 PMUX→1 个 PMUX+18 个宽 12 MUX；只去掉两路输出 PMUX，状态 PMUX 保留；FF、端口、原连接和原 wire 保留。15 个物理选择位对应状态码 1..15，无重复，7 组全宽交叉关系成立。 | [结构审计及脚本](../case_1/submit/structure/structure_audit.md)、[前 JSON](../case_1/submit/json/insertion_before.json)、[后 JSON](../case_1/submit/json/insertion_after.json)。归档审计结论与阶段内容可追溯，通用匹配器仍 `MISSING`。 |
| case1 whole-design EQY | 实际配置 `collect test1 group * bind *`，工作目录为一个合并分区 `test1.i0`；EQY `DONE (PASS, rc=0)`，SBY ABC PDR 有证明日志/PASS。 | [配置](../case_1/submit/eqy/rtlil_prototype.eqy)、[分区清单](../case_1/submit/eqy/rtlil_prototype.eqy_work/partition.list)、[日志](../case_1/submit/eqy/rtlil_prototype.eqy_work/logfile.txt)、[PASS](../case_1/submit/eqy/rtlil_prototype.eqy_work/PASS)、[DFF 形式模型](../case_1/submit/inputs/dffeas_formal.v)。这是归档 PASS，不是本次新跑或生产 Pass PASS。 |
| case3/4 包完整性 | 实际共 32 个文件，0 个 PASS 文件；清单声明无 ORIGINAL、全部 RECREATED。SHA256SUMS 只有缺失说明注释，没有有效哈希条目。 | [MANIFEST](../case_3_4/submit/submit/MANIFEST.md)、[STATUS](../case_3_4/submit/submit/STATUS.md)、[SHA256SUMS 占位文件](../case_3_4/submit/submit/SHA256SUMS)。已收到参考材料，但不能称“验证证据已补齐”。 |

case1 的资源/时间归档还给出插入点五次重放 CPU 中位数 `0.10→0.08 s`、峰值 RSS `31.55→30.42 MiB`，一次计时 ABC 外部 wall `46.601→40.514 ms`；从源流程的五次 CPU 中位数 `0.15→0.13 s`、RSS `33.89→32.33 MiB` 属于另一组实验，不能与前者拼接，更不能当作尚未实现 C++ Pass 的开销。[RESOURCE_AND_TIMING](../case_1/submit/RESOURCE_AND_TIMING.md)、[插入点逐次记录](../case_1/submit/json/insertion_cpu_rss_runs.json)、[源流程比较](../case_1/submit/full_compare.json)。`logs/rtlil_prototype.log` 和 `rtlil_replay.log` 的 `check` 为 0 问题，但 ABC 输出包含 `Abc_FrameUpdateGia(): Transformation has failed`，归档进程成功且 EQY PASS；保留此警告，不能写成工具零异常。[原型日志](../case_1/submit/logs/rtlil_prototype.log)、[重放日志](../case_1/submit/logs/rtlil_replay.log)。

### 2.2 case3/4 当前附件的阻断项与静态反例

1. **case3 夹具不是完整 gate。** `case3_simplified.v`/`case3_factored.v` 是组合 `sr/cs/gpioout` 参考夹具；官方 `test3.v` 含 `clk/sr_in/sr_out` 和状态保持逻辑。夹具整字赋值与官方未更新位的保持语义不能自动互换。不得以组合夹具的计数或局部 guard 说明代替官方 17 DFF 整设计 EQY。[夹具](../case_3_4/submit/submit/case3/inputs/case3_simplified.v)、[官方输入](../competition_case/test3.v)、[证明说明](../case_3_4/submit/submit/case3/proof/CASE3_PROOF.md)。
2. **case4 表存在确定反例。** 按官方输出拼接顺序 `{dataout[5:0],only_5b,datain_5bis7,data_dp,data_dn}` 核对全部 32 个地址，重建 `WORDS`/候选的 32/32 行不同。地址 0 官方为 `10'b1001110000`，重建为 `10'b1011010011`；在 `reset_n=1,ena=1,datain=0` 的采样时钟边沿输出词不同。即使补上原寄存器包装，该候选也违反 P0，必须拒绝。历史 RESULTS 的 26 数值仍保留为待证目标，但这份重建网表不是该结果的等价证据。[官方输入](../competition_case/test4.v)、[WORDS 定义](../case_3_4/submit/submit/case4/scripts/cofactor_normalize.py)、[重建网表](../case_3_4/submit/submit/case4/inputs/case4_winner_26.v)。
3. **表提取器未支持实际输入格式。** `truth_table_extract.py` 的正则仅匹配单个 `10'b...` RHS，官方 32 行实际为 `{6'b...,1'b...,1'b...,1'b...,1'b...}`，该匹配在官方文件为 0 行。不能用手写 `WORDS` 绕过提取失败；B 必须依据实际结构/官方拼接位序重新生成并核对完整表。[提取器](../case_3_4/submit/submit/case4/scripts/truth_table_extract.py)、[官方输入](../competition_case/test4.v)。
4. **旧附件中的阶段和 EQY 模板未闭合。** 两例 `baseline_13.ys`/`baseline_36.ys` 使用 `-run begin:map_luts`，停止点排他，未包含该阶段 ABC9，不能标为 post-ABC9 统计；EQY 模板含占位符，不能代替正式证据。该缺陷属于历史重建包；正式生产结论来自后续已验收提交。今后由 B 交实际命令和 gate，A 独立验收，不在附件中回写伪造结果。[case3 基线模板](../case_3_4/submit/submit/case3/yosys/baseline_13.ys)、[case4 基线模板](../case_3_4/submit/submit/case4/yosys/baseline_36.ys)。
5. **搜索来源不是原轨迹。** 重建脚本记录种子 `20261006`、500 随机候选和四轮贪心，但未交原始搜索日志；不能据此确认它复现了 RESULTS 中的“偏置随机”顺序或 26 LUT 获胜记录。[重建搜索](../case_3_4/submit/submit/case4/scripts/search_case4_mixed.py)、[winner_record](../case_3_4/submit/submit/case4/search/winner_record.json)、[原记录](../case_3_4/RESULTS.md)。

## 3. 工程现状、统计口径与工具链

### 3.1 当前实现状态

[工程状态 §5–10](工程状态.md)所述旧 A/B/C 联调已经有只读分析器、原子端口改写框架、19/19 微测试、1000/1000 固定种子和四官方例 EQY PASS；1000 种子中 72 个设计发生改写，共 252 个物理位。该验证属于当时的旧生产规则。状态文档还记录 B 的实验性整分支改写提交 `bff595118a130756f504af99c09000c98adda4ba`，但未给出它在四例达到本文目标的证据。最新四规则统一生产版本、重新执行的 19 微测试/1000 种子/四例回归均为 `MISSING`。

| case | 冻结基线 `$lut` / DFF / IO | 当前旧生产 Pass `$lut` | 最好已知候选与证据等级 | EQY 与生产状态 |
|---|---:|---:|---|---|
| 1 | 288 / 222 / 0 | 288 | 182 `$lut`；历史原型已归档，B 生产候选 `504df2a...` 本地复现相同结果并已推送。 | 待 A 独立 EQY/资源验收；`PENDING_A_AUDIT`。 |
| 2 | 25 / 0 / 0 | 25 | 15 `cycloneiv_lcell_comb`；B 生产候选 `504df2a...` 本地复现，post-ABC9 `$lut` 与厂商原语必须分列。 | 生产候选待 A 独立确认计分合法性、最终 gate 和 EQY；`PENDING_A_AUDIT`。 |
| 3 | 13 / 17 / 0 | 11 / 12 | 11 LUT/12 DFF；B `b8368b0...` 已完成历史独立验收。 | whole-design EQY PASS；当前默认生产路径已接受。 |
| 4 | 36 / 10 / 0 | 27 / 10 | 27 LUT/10 DFF；B `b8368b0...` 已完成历史独立验收。旧 26 LUT 错误表候选继续拒绝。 | whole-design EQY PASS；generation monitor、一次性授权和默认路径已接受。 |

基线与旧生产结果源于[工程状态 §3、§8](工程状态.md)；case1 新归档来源为[submit 结果](../case_1/submit/RESULTS.md)、[阶段 JSON](../case_1/submit/json/post_abc9_candidate.json)、[EQY 日志](../case_1/submit/eqy/rtlil_prototype.eqy_work/logfile.txt)；case2 来源为[补证结果](../case2_lut_opt_workspace/evidence_case2/RESULTS.md)；case3/4 历史值来源为[RESULTS §2、§9](../case_3_4/RESULTS.md)，本次补包不改变第 2.2 节的拒绝/缺证判断。上表不把任何“最好已知”提升为跨所有等价电路的全局最优。case2 的链式分解在**限定的单输出四输入 LUT 链类**达到每位 3 LUT 的下界；跨输出共享选择逻辑能否少于 15，证据未封闭。[case2 下界说明](../case2_lut_opt_workspace/optimality_argument.md)。

### 3.2 固定统计方法

正式可比的主指标是从官方 Verilog 开始，在 `synth_intel -family cycloneiv -top <top> -run begin:map_cells` 的 ABC9 后、厂商 `map_cells` 前，读取 `post_abc9.json` 中**指定顶层模块**的 `cells[*].type == "$lut"` 个数。不能取 JSON 的第一个模块；ABC9 会带入辅助模块。示意命令：

```sh
for n in 1 2 3 4; do
  yosys -Q -p "read_verilog competition_case/test${n}.v; synth_intel -family cycloneiv -top test${n} -run begin:map_cells; stat; write_json out/case${n}/post_abc9.json"
  jq --arg top "test${n}" '[.modules[$top].cells[] | select(.type == "$lut")] | length' "out/case${n}/post_abc9.json"
done
```

命令是四例统一验收模板，**尚未作为四例统一回归运行**；case2 的短流程阶段综合/EQY 另见[补证清单](../case2_lut_opt_workspace/evidence_case2/MANIFEST.md)。正式回归须记录输出路径、返回码、版本与官方输入哈希。[工程状态 §1、§3](工程状态.md)、[RESULTS §3–4](../case_3_4/RESULTS.md)。case4 从中间 RTLIL 续跑曾得到 35 而官方源完整流程为 36，故中间截点只能做诊断，不能代替正式比较。[RESULTS §4](../case_3_4/RESULTS.md)。

同时保存厂商映射后 `cycloneiv_lcell_comb` 计数，使用完整 `synth_intel -family cycloneiv` 后的指定顶层 JSON 或 Yosys `stat`；必须与 `$lut` 分列。case1 文档给出 `$lut` 288→182、完整原语 286→180。case2 的[阶段 JSON 与计数](../case2_lut_opt_workspace/evidence_case2/stage_counts.csv)已补：基线 25 `$lut` → 25 原语；行为级候选 25 `$lut` → 25 原语；显式原语候选在 ABC9 前后均为 0 `$lut` + 15 原语。**显式原语路径的 0 `$lut` 不代表零成本，亦不是 15 `$lut`。** `generated/yosys_lut6.log` 的 10 LUT6 是 `abc -lut 6` 参考模型。[case1 方法 §2](../case_1/CASE1_182LUT_特别详细方法说明.md)、[case2 补证](../case2_lut_opt_workspace/evidence_case2/RESULTS.md)。

本次 case1 已有 JSON 的只读复核等价于下面计数，不触发综合：

```sh
for stage in post_abc9_baseline post_abc9_candidate vendor_baseline vendor_candidate; do
  jq '.modules.test1.cells | group_by(.type) | map({type: .[0].type, count: length})' \
    "case_1/submit/json/${stage}.json"
done
```

Windows 旧附件审计实际以 `Get-Content -Raw | ConvertFrom-Json -AsHashtable` 读取，并按 `modules['test1']['cells']` 的 `type` 计数；须保留大小写区分（原 JSON 同时有 `I0/i0`），不能用丢失该区分的解析器。旧 case3/4 重建包无阶段 JSON，仍不能把脚本名的 `11/26` 当作输出；正式生产计数以已验收提交重新生成的 JSON/日志为准。

工具链事实和缺口：

| 工具 | 已见证据 | 正式锁定状态 |
|---|---|---|
| Yosys | [case2 工具链记录](../case2_lut_opt_workspace/evidence_case2/toolchain.txt)：0.69、完整提交 `9f75ca1f9834a39a863915b5dae0c7b1e33533bc`、二进制 SHA-256。case1 [PROVENANCE](../case_1/submit/PROVENANCE.md)也记录该完整源码提交及本机路径。 | case1 二进制 SHA-256/与 case2 同一构建的证明 `MISSING`；case3/4 只有套件/短版本陈述，精确工具记录仍缺。 |
| ABC / `yosys-abc` | [case2 工具链记录](../case2_lut_opt_workspace/evidence_case2/toolchain.txt)：ABC 1.01、完整提交 `a51bf4a7bb38ce06ea3a544b1463b40832213917`、二进制 SHA-256。case1 实际 ABC 日志存在。 | case1/3/4 原实验 ABC 的完整提交/二进制 SHA-256 `MISSING`；不得借用 case2 的身份记录。 |
| EQY | [case2 工具链记录](../case2_lut_opt_workspace/evidence_case2/toolchain.txt)：v0.69、完整提交 `4a72eb94fc253062464afee4d0018359017bb846`、二进制 SHA-256。case1 有历史工作目录和 PASS；case3/4 正式生产验收使用固定 EQY 并已归档。 | case1/2 新生产候选仍需 A 在当前固定工具链下独立生成完整工作目录、PASS 和二进制哈希闭环。 |
| SBY | M0 指定 v0.69、`b1a1e98cba941ec8433f8dc27f416cd7bb7f14be`；case1 [SBY 日志](../case_1/submit/eqy/rtlil_prototype.eqy_work/strategies/test1.i0/prove/test1.i0/logfile.txt)记录实际 ABC PDR PASS。 | “实际日志不存在”的旧缺口已消除；精确 SBY 版本、完整提交和可执行 SHA-256 仍 `MISSING`。 |

版本固定值来自[M0 清单 §5](M0三人协作启动检查清单.md)；不得从 Yosys 的短 SHA 推断 EQY/ABC 精确提交。[工程状态 §2](工程状态.md)。

## 4. case 到结构规则的映射

下表只描述**设计时提炼的结构特征**。`case` 列仅是验收标签，运行时规则不能读取它。

| case | 算法材料路径 | 算法摘要：识别、变换、边界、共享与风险 | 基线 LUT | 最好已知结果与证据等级 | EQY 状态 | 生产实现状态 |
|---|---|---|---:|---|---|---|
| 1 | [详细方法](../case_1/CASE1_182LUT_特别详细方法说明.md)、[submit 清单](../case_1/submit/MANIFEST.md)、B 候选 `504df2a...` | 识别两只共享状态译码、同宽且输出独立的 PMUX；证明全状态覆盖及交叉关系；以两棵半规模树和末级交换重建。运行时只看结构。 | 288 `$lut` | 182 `$lut`，最好已知归档实测；非全局最优 | 历史原型 PASS；B 生产 gate 待 A 重验 | 通用生产候选已实现；`PENDING_A_AUDIT` |
| 2 | [方法](../case2_lut_opt_workspace/OPTIMIZATION_ALGORITHM_AND_REPRODUCTION.md)、[补证](../case2_lut_opt_workspace/evidence_case2/RESULTS.md)、B 候选 `504df2a...` | 识别有序优先 MUX 链，保持重叠选择赢家；按位生成三段 LUT4 原语。边界为链结构、内部扇出、原语合法性和四态语义。 | 25 `$lut` / 25 原语 | 15 `cycloneiv_lcell_comb`；10 LUT6 为异口径参考 | B 自测 EQY PASS；待 A 独立重验 | 通用生产候选已实现；`PENDING_A_AUDIT` |
| 3 | [RESULTS](../case_3_4/RESULTS.md)、正式 B `b8368b0...` | 识别 guard 支配的比较/决策锥，按冻结证明重建并保持时序边界。 | 13 `$lut` / 17 DFF | 11 LUT / 12 DFF，当前生产结果 | whole-design EQY PASS | 已生产并历史独立验收 |
| 4 | [RESULTS](../case_3_4/RESULTS.md)、正式 B `b8368b0...` | 从实际结构恢复常量表，绑定快照、generation monitor 和一次性授权；旧错误表拒绝。 | 36 `$lut` / 10 DFF | 27 LUT / 10 DFF，当前生产结果；历史 26 未闭合 | whole-design EQY PASS | 已生产并历史独立验收 |

## 5. 单入口统一算法架构

正式生产入口是同一个 `pmux_opt` 调度器，在冻结的 `memory_map; opt -undriven -fine` 之后、公共 `techmap -map +/techmap.v` 之前运行；候选提交后由综合流程显式 `opt_clean`。若规则二采用厂商原语或规则四采用特殊 ABC9 脚本，仍由**同一调度器和同一候选协议**决定，映射后再统一计数和 EQY；不能按 case 名调用独立脚本。[v0.1 §2、§4](PMUX优化技术方案_v0.1.md)、[case1 插入点](../case_1/CASE1_182LUT_特别详细方法说明.md)。

```text
只读结构分析 → 特征提取 → 结构规则匹配 → 确定性候选生成
→ 同一证明服务 → 原子改写计划 → 成本门 → whole-design EQY 门
→ 合格候选的确定性选择 → 原子提交；任何门失败则回退
```

1. **结构分析与特征提取。** `SigMap` 归一化连接，建立单驱动/扇出、选择优先级、比较器、时序边界、表真值和属性索引；按稳定模块/cell/端口顺序枚举。模块名称只用于日志和 EQY top 定位，不参与匹配。任意没有匹配结构的输入直接产出“零候选、原网表”。
2. **规则匹配。** 四类规则均可独立发现候选，但都返回统一 `Candidate{source_snapshot, affected_cells, proof_obligations, rewrite_plan, cost_estimate, rollback_data}`。规则库可分文件，入口和交易协议必须相同。匹配拒绝只影响当前候选。
3. **证明。** 复用 v0.1 三值/比较器/定点证明链；结构相等用规范化逐位连接和穷尽状态表，非平凡关系可在明确预算内调用 SAT。一个候选的组合锥沿用每入口 `512 cell / 8192 bit / 262144 event` 硬上限；新规则若需要额外搜索/查询预算，先冻结数值和测试。SAT 仅在便宜证明未知且边界清晰时调用；只有 UNSAT 授权，SAT/UNKNOWN/超时/错误均拒绝。[v0.1 §0.6、§4](PMUX优化技术方案_v0.1.md)。
4. **冲突顺序。** 先按影响范围分组，重叠候选不得同时生效。组内按“证明完整 → P0 可验证 → 映射后成本低 → 改动范围小 → 稳定结构编号小”排序。`rewrite_conflict` 拒绝冲突计划；同一快照下两个候选均可行时，以真实、同口径的 LUT/DFF/IO 成本决定，平局采用固定稳定键。禁止依赖发现顺序。
5. **成本与 EQY。** 快速结构门只判合法性，不能声称预测 ABC9 LUT。候选在隔离网表中完成全流程 ABC9/厂商映射后计数，并以官方 RTL 为 gold、该候选最终 gate 为 gate 做 whole-design EQY。成本未严格改善或任一官方 case 违反已锁定验收基线时，默认规则关闭。若离线选出的默认规则集合需要四例 P2 保证，四例必须在相同工具链重新验收；运行时对任意输入仍用统一成本/证明判定。
6. **提交与回退。** 匹配、证明、成本、EQY 均在同一只读快照或隔离副本上完成；提交前复查 cell 类型、参数、完整端口、属性和驱动快照。通过才原子提交；任何证明失败、事实冲突、预算耗尽、`rewrite_conflict`、成本不降、EQY 失败或证据缺失都零写入并记录原因。标准 `opt_clean` 只清除真正失去扇出的逻辑；共享节点保持。

上述框架是 v0.2 **设计**，不是现有生产 Pass 的完成声明。尤其在线逐候选执行全设计 EQY/ABC9 的成本目前没有测量；实现可先以隔离构建进行离线候选筛选，再把固定且全门通过的规则纳入默认流程，不能省略正式 EQY 门或把代理成本当正式 LUT。[工程状态 §8–12](工程状态.md)。

### 5.1 可执行的统一算法步骤

下表规定**一个设计输入**经过**同一入口**的完整处理次序。规则族只产生候选，不能自行提交网表、调用另一套独立验收流程，或用 case 名选择算法。所有候选和回退均带来源设计指纹、候选编号及原因码；具体数据结构在接口冻结阶段定稿，不能把本表中的字段名称当成已实现接口。

| 步骤 | 输入 → 操作 → 输出 | 失败时的确定动作 | 主责 |
|---|---|---|---|
| 0. 冻结输入 | 官方/任意 RTLIL、当前 selection、目标工艺及统一预算 → 记录模块端口、全部相关 cell 的类型/参数/连接/属性/驱动快照和设计指纹 → 不可变快照 | 输入不完整或有受保护/多驱动目标则拒绝该目标 | B 生成；A 独立归档并核验输入哈希 |
| 1. 建立结构索引 | 快照 → `SigMap`、有序选择链、状态译码、比较器与 guard、表真值、共享扇出和时序边界 → 结构索引 | 缺失结构标记为“不匹配”，保留原网表 | B 枚举；A 审核事实含义 |
| 2. 枚举四族候选 | 结构索引 → 按稳定 cell/端口/分支顺序套用第 6 节前提 → 候选及其影响锥/证明义务 | 任一前提不符则只拒绝该候选；不按文件、top、module 或 case 编号分派 | B 调度和实现；A 审核结构谓词与反例 |
| 3. 生成只读计划 | 候选 → 原 cell 快照、预计新增/删除 cell、目标连接、共享节点保留表及复原信息 → 完整计划 | 计划覆盖时序边界、保护属性或选区外对象则拒绝 | B 生成；A 只读复核 |
| 4. 局部证明 | 计划和守卫 → 三值事实、比较器反推、定点传播、必要时有界 SAT、逐位/逐状态证明链 → `PROVEN` 或原因码 | `UNKNOWN`、冲突、预算耗尽、SAT 非 UNSAT 均拒绝，不降格为通过 | A 负责证明和反例；B 只消费冻结结果 |
| 5. 冲突仲裁 | 同一快照下所有 `PROVEN` 计划 → 构造影响 cell/端口位及共享扇出冲突图，按稳定键选择互不冲突的候选集 → 候选批次 | `rewrite_conflict` 拒绝相冲突计划；被拒计划不影响其它独立计划 | B |
| 6. 隔离试改 | 候选批次 → 复制网表、复核快照、一次性提交计划并 `opt_clean; check` → 隔离候选 gate | 任一预检或 `check` 失败，丢弃隔离副本，原网表零写入 | B |
| 7. 统一映射成本门 | 每个隔离 gate 与相同工具链下原 gate → ABC9 后指定顶层 `$lut`、`map_cells` 后原语、DFF/IO、CPU/内存分列计数 → 可比成本记录 | 按同一合法计分口径无严格收益、DFF/IO 回退或预算超限，拒绝候选；显式原语的 0 `$lut` 不能按零成本排序 | B 预跑；A 独立重跑并作准入判定 |
| 8. whole-design EQY 门 | 官方 RTL/相应设计的原输入为 gold，隔离候选的最终 gate 为 gate → 固定 EQY 配置、日志、PASS、哈希 → 等价证书 | FAIL、UNKNOWN、超时、工具错误或证据缺失都拒绝候选 | B 预跑；A 独立重跑并定位反例 |
| 9. 选定并原子提交 | 对通过步骤 0–8 的批次，按真实成本、改动范围、稳定编号排序 → 提交前再次核对原快照 → 一次性提交 | 快照变化、无合格候选或最终确认失败，返回原网表；不可部分提交 | B |
| 10. 四例离线准入 | 同一已集成规则集合及固定工具链 → 19 微测试、1000 种子、四例 EQY 和 LUT/DFF/IO 对照 → P0/P1/P2 验收报告 | 任一官方例回退或缺证据，规则集合不默认启用；P3 调参同样重新验收 | A 独立运行并判定；B 只提供复现说明和修复 |

步骤 7–8 对新规则的**准入和最终验收**必须真实运行；在线逐候选运行的资源预算及缓存策略尚未冻结，标为 `UNVERIFIED`。运行时绝不能把未经步骤 8 验证的候选记为通过。成本先比正式目标口径，再以其它资源与稳定键打破平局；若口径未定（case2 的 `$lut` 与原语差异），候选停在步骤 7，不能伪造跨口径收益。四例验收编号仅由步骤 10 的测试 oracle 使用。[case2 阶段计数](../case2_lut_opt_workspace/evidence_case2/stage_counts.csv)。

## 6. 四类结构规则族

“通用”仅表示由结构条件触发。每类保留必要的结构特化，并统一使用“规则名、结构前提、证明义务、改写内容、成本门、失败回退、证据状态、对应验收 case”八字段。

### 6.1 规则族一：共享状态选择的双 PMUX 重建

**规则名：** paired shared-select PMUX reconstruction。证据源：[case1 详细方法 §2–10](../case_1/CASE1_182LUT_特别详细方法说明.md)。

**结构前提：** 两个完整选中的 `$pmux` 共享逐位相同的 `S`，数据宽度均为 `W`，输出 `Y` 不重叠。每个 `S[i]` 经驱动追溯为同一 `m` 位状态向量与无 `x/z` 常量的 `$eq`；按实际 `A_WIDTH/B_WIDTH`、`A_SIGNED/B_SIGNED` 的扩展语义解释常量，比较不明就拒绝。两侧状态编码均恰好覆盖 `1..2^m−1` 且无重复，状态 0 走各自默认 `A`。按**物理**索引取 `D8(c)=pmux8.B.extract(index_of_code_c*W,W)`、`D9(c)=pmux9.B.extract(index_of_code_c*W,W)`；RTLIL 文本从左到右的打印顺序不是 `S[0..]`。必须经 `SigMap` 全宽逐位验证：对 `n=1..2^(m−1)−1`，`D8(2n)=D9(2n+1)`、`D9(2n)=D8(2n+1)`；第 0 组使用 `stored_X0=D9(1)`、`stored_Y0=D8(1)`、`live_X0=A8`、`live_Y0=A9`。匹配不得读取文件名、模块名、输出名、源码行号或自动 cell 名；任一条件失败，两侧都不改。

**证明义务：** 穷尽全部 `2^m` 个确定状态；逐状态比较两路下一数据；证明状态 0 使用当前输入而非寄存器旧值、状态 1 捕获数据交叉、`state[0]` 仅作末级交换；保持状态转移、`valid`、复位、使能、DFF、模块端口和输出触发器边界。带符号扩展、`x/z`、多驱动、别名不明或仅逻辑等价但连接不同者首版拒绝，除非另有受预算约束的 SAT 证明。完成后仍须 whole-design EQY。[case1 方法 §3、§5–6](../case_1/CASE1_182LUT_特别详细方法说明.md)。

**改写内容：** 对已验证的 `m=4,W=12` 结构，令 `p=state[0]`、`k=state[3:1]`：

```text
X0 = p ? D9(1) : A8
Y0 = p ? D8(1) : A9
对 n=1..7：Xn=D8(2n)，Yn=D9(2n)
U=SELECT8(X0..X7;k)
V=SELECT8(Y0..Y7;k)
new_D8=p ? V : U
new_D9=p ? U : V
```

第 0 组特例放在叶子；两树的底、中、根层依次用状态位 `[1]`、`[2]`、`[3]`，审查默认树位序为 `[3,2,1]`。`new_D8/new_D9` 连回原两个 PMUX 的 `Y`，由原输出 DFF 的 `D` 消费，不能绕过 DFF。只删除这两个输出 PMUX，不触碰下一状态 PMUX、状态逻辑、复位、使能、DFF 或端口。生成 2 个叶子、7+7 个树节点和 2 个末级交换，共 **18 个宽 12 的 `$mux`**。一般化为两棵 `2^(m−1)` 路树；`m≠4` 或 `W≠12` 的收益仍须单独 EQY/ABC9 验证。[case1 方法 §4、§7](../case_1/CASE1_182LUT_特别详细方法说明.md)。

**插入位置与成本门：** 在 `synth_intel.cc` 的 `map_ffram` 中 `memory_map; opt -undriven -fine` 后、`techmap -map +/techmap.v` 前调用统一 Pass，随后显式 `opt_clean`。该点原两输出 PMUX 已由早期 `S_WIDTH=16,A=x` 变为 `S_WIDTH=15,A=当前 I0/Q0`。先做完整覆盖、全宽关系、单驱动、属性和预算结构门；18 个粗粒度 MUX 多于原 2 PMUX **不是**否决依据。正式成本门使用 ABC9 后顶层 `$lut` 和完整原语计数，记录逐例 CPU、ABC 时间、峰值内存；四例 EQY PASS 且无资源回退才允许默认启用。若输出依赖新 cell 顺序，固定生成顺序并保存阶段网表。[case1 方法 §2、§8](../case_1/CASE1_182LUT_特别详细方法说明.md)。

**失败回退：** 状态码缺失/重复、比较扩展不明、重叠选择、缺一侧分支、交叉关系某位失败、`x/z`、多驱动、`keep`、输出额外驱动、预算耗尽或提交前快照不符，均保持原网表并给出原因。两只 PMUX 的类型、参数、完整端口和连接必须在统一只读计划中预检后再一起提交，不允许改完一侧再发现另一侧失败。

**证据状态与对应验收 case：** case1 submit 的阶段 JSON 独立复核为 `$lut` **288→182**、`cycloneiv_lcell_comb` **286→180**、`dffeas` **222→222**；实际原型 whole-design EQY 配置/日志/PASS 和 gold/gate 已归档，322 项文件哈希通过。该结果为“最好已知归档实测”，不是生产 Pass 已实现或全局最优。源 Yosys 提交有记录，精确 ABC/EQY/SBY 构建身份仍 `MISSING`。状态 0 在末端为 202、one-hot 为 184、混合分组为 182、内部交换为 249–321、条件加一为 200、已测试 ABC9 `-W=50..1000` 均未低于 182，已有 sweep JSON/日志归档。默认选叶子特例加末级交换的 `[3,2,1]`；其它结构仍需同门禁。[阶段结果](../case_1/submit/RESULTS.md)、[EQY](../case_1/submit/eqy/rtlil_prototype.eqy_work/logfile.txt)、[搜索追溯](../case_1/submit/SEARCH_TRACEABILITY.md)、[tree sweep](../case_1/submit/search/artifacts/tree_sweep.json)、[W sweep](../case_1/submit/search/artifacts/w_sweep.json)。

**族一落地与验收步骤：** ① 历史 182 候选附件只作为拓扑和结果来源；② B 在统一结构索引上配对同 `S`、同 `W` 的候选，验证覆盖和交叉关系并形成快照；③ A 审核逐状态、逐位关系和 DFF 边界；④ B 创建 18 个 MUX并原子替换两只 PMUX；⑤ A 独立用完整 ABC9、厂商映射和 whole-design EQY 复核 182/180/222 及其它三例无回退。B 候选 `504df2a...` 已完成实现自测，但 W=1/12/32、拒绝矩阵和统一 P2 仍以 A 独立审计结果为准。

### 6.2 规则族二：priority mux 的四输入 LUT 链式分解

**规则名：** priority mux LUT4 chain decomposition。证据源：[case2 方法](../case2_lut_opt_workspace/OPTIMIZATION_ALGORITHM_AND_REPRODUCTION.md)、[生成器](../case2_lut_opt_workspace/cycloneiv_opt.py)、[已生成网表](../case2_lut_opt_workspace/generated/case2_cycloneiv_lut4_explicit.v)。

**结构前提：** 从 `$mux` 链或经明确证明具有优先语义的选择网络提取有序分支、宽度、默认值和每位独立数据。case2 实例为四路优先级 `a>b>c>d>0`、5 位输出；重叠选择必须由优先级实现决定赢家。原生 `$pmux` 在多选时为未定义/X，不能未经语义证明直接套用 `$mux` 优先级；`full_case/parallel_case` 属性不能代替证明。选择 `x/z`、非组合反馈、数据/默认值扩展不明时拒绝。

**证明义务：** 所有确定选择组合下保持原优先级，逐位证明 LUT 真值表与原函数相同，并确认正式四态/undef 模型是否允许该原语映射。补证中的 [LUT4 专用验证](../case2_lut_opt_workspace/evidence_case2/verification/lut4_verification.json)穷举 `5×2^8=1280` 个位模式；原 `verify_case2.py` 直接执行时默认验证 LUT6 参考网络，二者不得混称。[EQY 配置](../case2_lut_opt_workspace/evidence_case2/explicit.eqy)用官方 RTL 为 gold、显式原语网表为 gate，日志 PASS；统一 Pass 生成的最终 gate 仍须新 EQY。

**改写内容：** 每位 `s0=c?datac[i]:(d?datad[i]:0)`、`s1=b?datab[i]:s0`、`out[i]=a?dataa[i]:s1`。四输入模型的动态规划选 `(0,2),(2,1),(3,1)` 三段；按 `dataa + 2·datab + 4·datac + 8·datad` 的真值表索引生成底层掩码 `16'hE4A0`，上两层各 `16'h00D8`。5 位共 15 个 `cycloneiv_lcell_comb`。行为级写法经 ABC9 重映射仍为 25 `$lut`、最终 25 原语；15 原语的实测依赖显式实例化，ABC9 跳过该类原语。[case2 方法 §4–8](../case2_lut_opt_workspace/OPTIMIZATION_ALGORITHM_AND_REPRODUCTION.md)、[阶段计数](../case2_lut_opt_workspace/evidence_case2/stage_counts.csv)。

**成本门：** 同时比较正式 post-ABC9 `$lut` 与完整厂商原语数；显式候选的 `0 $lut + 15 cycloneiv_lcell_comb` 与行为级候选的 `25 $lut → 25 cycloneiv_lcell_comb` 是两种不同路径。若正式评分只允许技术无关改写且必须由 ABC9 从 `$lut` 生成原语，当前行为级方案无收益，达到 15 的合法统一生产路径为 `MISSING`。须先确认显式原语可进入共同插入点及计分链，再按正式口径作成本门。

**失败回退：** 优先级证明失败、PMUX 重叠不明、LUT 掩码校验失败、显式原语不合法、完整流程成本不降或 EQY 未 PASS 时保留原网络。

**证据状态与对应验收 case：** 历史补证确认行为级候选为 25 `$lut`，显式候选为 0 `$lut` + 15 原语，两条独立 EQY 均 PASS。B 候选 `504df2a...` 已实现结构驱动的统一生产路径并在自测中得到 15 原语；15 只在限定 LUT4 链类有下界，不是一般多输出网络的全局最优。A 尚需独立确认正式计分合法性、最终 gate、EQY 和反例，因此状态为 `PENDING_A_AUDIT`。[最优性范围](../case2_lut_opt_workspace/optimality_argument.md)。

**族二落地与验收步骤：** ① B 从输入结构恢复有序优先级链、默认值、分支数据和内部扇出，显式拒绝不满足结构前提的网络；② B 按 LUT4 输入上限生成分段和每位掩码；③ A 审核全部选择重叠组合、链方向、位序、原语语义、预算和拒绝例；④ B 只在冻结条件通过时生成合法厂商原语并保持快照；⑤ A 对最终 gate 分列计数并跑 whole-design EQY。B 候选已完成实现自测，A 独立验收前不得把 15 原语写成最终 P2 PASS。

### 6.3 规则族三：guard 条件下比较器缩宽与选中数据折叠

**规则名：** guarded comparator narrowing and selected-data folding。证据源：[case3/4 RESULTS §5](../case_3_4/RESULTS.md)。

**结构前提：** 有一组等值比较器和对应选择数据，同一控制流的外层 guard 在**当前周期**支配该组选择，且可证明部分被比较位在 guard 下恒定。case3 材料中的具体见证是 `rw=sr[7]` 且进入分支需 `cs=1,rw=1`；因此 `sr[7]=1`。匹配只能从 alias/支配关系和比较端口提取，不能查 `test3`、`sr`、`8'hAA` 等名字或固定常量。

**证明义务：** 对每个被删比较位证明 `G⇒X[k]=C[k]`；缩宽后的比较必须在全部可观察路径下与原比较相同。逐位证明选中数据常量，默认数据中自由位保留。比较常量需按宽度、符号扩展语义解释，比较共享扇出不能误删；重建决策优先关系、复位、使能、DFF 和端口不变。若只能在外层 guard 下等价，需证明该锥确实仅在 guard 下可观察；不能把局部条件当成全局事实。

**改写内容：** 见证结构中 `sr==8'hAA/FF/FE` 在 `sr[7]=1` 下依次缩成 `sr[6:0]==7'h2A/7'h7F/7'h7E`；相应选择数据折为 `8'h55/4'hF/4'hF`，默认的 `sr[0]` 保持自由。通用规则的输入是已证明的 guard 和实际比较常量；缩宽、数据折叠、决策重建在同一原子计划提交。`opt_clean` 后可把 `pmuxtree` 作为受成本门约束的通用清理候选。[RESULTS §5.1–5.4](../case_3_4/RESULTS.md)。

**成本门：** 后续 ABC9 顶层 `$lut` 必须严格降低，四例非目标资源和 EQY 不回退；代理结构成本仅用于候选排序。**失败回退：** guard/支配关系、比较语义、数据位、预算、成本或 EQY 任一不明，零写入并保持旧规则。

**证据状态与对应验收 case：** 历史报告称原始+`pmuxtree` 为 13→12、guard 夹具为 13→11；11 是报告中的最好已知结果，当前仍 `UNVERIFIED`。重建 `case3_simplified.v`、`case3_factored.v`、guard/搜索脚本和 EQY 模板已交，但不是原候选 IL/JSON/执行日志，且组合夹具没有保留官方整设计状态/未更新位。实际 13/12/11 网表、真实 gold/gate、EQY 工作目录/PASS 和有效哈希清单为 `MISSING`；生产 Pass、19 微测试与 1000 种子的新规则结果仍缺。[历史 RESULTS](../case_3_4/RESULTS.md)、[包状态](../case_3_4/submit/submit/STATUS.md)、[夹具](../case_3_4/submit/submit/case3/inputs/case3_simplified.v)、[缺件表](../case_3_4/submit/submit/case3/artifacts/MISSING.md)。

**族三落地与维护步骤：** ① B 维护保持官方时序、未更新位和决策锥边界的生产候选；② A 在结构索引上核验当前周期支配 guard `G` 及每个候选事实 `G⇒X[k]=C[k]`，确认 `¬G` 下不可观察或保留原比较；③ B 只消费已冻结证明，构造缩宽比较、数据折叠和保留默认自由位/未更新位的原子计划；④ A 审核优先决策、共享扇出与 DFF 边界；⑤ A 独立对最终 gate 做 whole-design EQY 和 ABC9 成本对照。现有已验生产结果为 11 LUT/12 DFF；任何后续改动重新走 B 提交、A 审计流程。

### 6.4 规则族四：有界常量查找表的 Shannon cofactor 分解与共享搜索

**规则名：** bounded constant-table cofactor decomposition。证据源：[case3/4 RESULTS §6](../case_3_4/RESULTS.md)。

**结构前提：** memory mapping 后存在有限地址位、完整常量表/等价 MUX 锥、固定输出位序，能从结构恢复逐输出真值表。输入不含未解释的 `x/z`、多驱动、隐藏存储写入或跨时钟组合反馈。case4 的见证结构为 5 位无符号地址、10 位注册输出；这只是结构容量实例，不作为运行时身份条件。

**证明义务：** 对每个输出 `f` 和选择位 `x`，完整真值表满足 `f=x?f1:f0`，且 `f0/f1` 对剩余地址位的位序正确；cofactor 去重仅合并相同 `(support,truth_table)`。保持异步低有效 `reset_n`；原复位分支中 `dataout=0`、`only_5b=1`、`data_dp=0`、`data_dn=0`，**`datain_5bis7` 未被赋值而必须保持原语义**；非复位时 `ena=0` 将十位拼接输出全赋零，`ena=1` 锁存选中表项。任何时序边界疑义拒绝。[RESULTS §6.1、§6.5](../case_3_4/RESULTS.md)。

**改写内容：** 有界提取真值表，逐输出选一个分解位、生成两个最多四输入的 cofactor，再 MUX 合成；对常量 cofactor 消除，对相同 cofactor 做跨输出共享。为候选选择使用固定种子和严格预算的搜索；报告中的获胜向量 `[3,3,1,4,4,4,0,2,4,4]` 只能是离线实验结果或回归 oracle，运行时不得以 case 身份或该向量硬编码。报告的搜索范围为 500 个确定性偏置随机向量、5 个统一位候选和最多四轮严格改进贪心；不是穷尽 `5^10`。[RESULTS §6.2–6.3](../case_3_4/RESULTS.md)。

**成本门：** 候选按顶层 `$lut` 比较，保留完整 DFF/IO、CPU/内存；报告中的候选使用 `map_ffs:map_luts` 后 `abc9 -lut 4 -W 300 -script`，脚本体为 `&scorr; &sweep; &dc2; &dch -e -r; &if -a -W 300 -v; &mfs; &mfs`。该脚本是可配置映射候选，不能取代逻辑证明；与其他三例共用同一验证门。[RESULTS §6.4](../case_3_4/RESULTS.md)。

**失败回退：** 表提取、位序、复位/保持、搜索预算、快照、EQY 或 LUT 门任一失败，保持原网表；不启动无界搜索。

**证据状态与对应验收 case：** 历史报告称 36→26 `$lut`、whole-design EQY PASS，26 是报告中的最好已知有界结果，但本包仍 `UNVERIFIED`。本次已交 RECREATED 搜索/cofactor/短重放脚本、组合候选、winner_record 和 EQY 模板；它们没有实际运行日志、阶段 JSON/IL 或 EQY PASS，且重建表 32/32 行与官方不同，**当前重建候选 REFUTED / P0 拒绝**（第 2.2 节）。先恢复正确真值表和原时序包装，才允许进入成本/EQY 验收；生产 Pass 与新规则四例回归仍 `MISSING`。[历史 RESULTS](../case_3_4/RESULTS.md)、[重建来源](../case_3_4/submit/submit/MANIFEST.md)、[重建表](../case_3_4/submit/submit/case4/scripts/cofactor_normalize.py)、[缺件表](../case_3_4/submit/submit/case4/artifacts/MISSING.md)。

**族四落地与维护步骤：** ① B 在 `memory_map` 后从实际 MUX/常量结构恢复带地址/输出位序的真值表，不复用旧错误静态表；② A 逐输出对全部确定地址校验，任何一项不符均零改写；③ B 在冻结预算内生成/选择候选并保持寄存器、复位、使能和保持行为；④ A 对结构相等做穷尽表证明，对不确定行为保守拒绝；⑤ B 原子提交并绑定快照、generation monitor 和一次性授权，A 用官方 EQY、ABC9、DFF/IO 和生命周期反例完整复核。现有已验生产结果为 27 LUT/10 DFF；历史 26 LUT 仍是未闭合实验目标。

## 7. 运行时算法与验收 oracle 分离

运行时输入只提供 RTLIL 设计、目标工艺和统一预算；特征提取只读取 cell 类型、参数、端口、连接、属性约束及已证明的逻辑关系。`module_id`、top 和输入路径可用于定位设计及日志，但不能进入规则触发谓词。验收系统允许读取下表中 case 名和预期数值；该表绝不能编译成运行时 `if case_id` 分派。

| case | 预期最好已知目标 | whole-design EQY 预期 | 证据等级与当前门禁 | 允许的结构规则族 | 禁止的实现方式 |
|---|---:|---|---|---|---|
| 1 | 182 `$lut`；另记 180 `cycloneiv_lcell_comb` | PASS，官方 RTL 对最终 gate | 最好已知归档结果，322 哈希/JSON/EQY 已核验；统一生产 Pass 尚未达标，工具身份仍有缺件 | 共享状态选择的双 PMUX 重建；其它规则只在独立证明且不回退时可用 | 按模块/文件/case 编号、自动 cell 名或源码行号触发 |
| 2 | 15 `cycloneiv_lcell_comb`；行为级候选实测 25 `$lut`，显式候选 0 `$lut` + 15 原语 | PASS，官方 RTL 对最终 gate | 两个实验候选 EQY PASS；统一 Pass gate 与四例共同验收 `MISSING` | priority mux LUT4 链式分解；其它规则须保优先级 | 按名称触发；把 10 LUT6 或 15 原语直接当成 15 `$lut` |
| 3 | 历史目标 11 `$lut` | PASS，官方 RTL 对最终 gate | 报告中的最好已知；RECREATED 组合夹具不构成整设计证据，实际网表/EQY/PASS/哈希 `MISSING` | guard 下比较器缩宽/数据折叠；其它规则若不回退可用 | 按名字/固定 AA、FF、FE 常量触发；把夹具代替官方时序设计 |
| 4 | 历史目标 26 `$lut` | PASS，官方 RTL 对最终 gate | 历史有界最好已知仍 `UNVERIFIED`；当前错误重建候选 P0 拒绝，原获胜网表/EQY `MISSING` | 有界常量表 cofactor 搜索；其它规则若不回退可用 | 按名字/固定向量触发；在运行时硬编码静态表；忽略表不一致 |

P2 的当前判定仍是 **未通过/UNVERIFIED**：case3/4 已完成历史独立验收，case1/2 已有 B 生产候选，但尚无 A 对 case1/2 精确提交的独立审计、四例统一验收和最终合并证据。case2 的 15 是显式 `cycloneiv_lcell_comb` 数，不是 15 `$lut`；A/B 必须在规则冻结前确认正式计分链合法性并分列报告。[工程状态](工程状态.md)、[case2 补证](../case2_lut_opt_workspace/evidence_case2/RESULTS.md)、[双人契约](G3双人协作与接口契约.md)。

## 8. 优先级冲突处理

| 冲突场景 | 高优先级要求 | 低优先级要求 | 处理结果 | 依据 |
|---|---|---|---|---|
| 重建 case4 标注目标 26 LUT，但表与官方 32/32 行不同 | P0：语义等价 | P2：达到历史 26 | 直接拒绝当前候选，保留原网表；先恢复正确表及整设计 EQY，不能用目标数覆盖反例。 | 第 2.2 节；[官方表](../competition_case/test4.v)、[重建表](../case_3_4/submit/submit/case4/scripts/cofactor_normalize.py) |
| 新 ABC9 脚本使 case3 更小，但 case1 的最终 gate EQY 未 PASS | P0：四例 whole-design EQY PASS | P3：脚本收益 | 拒绝该脚本作为默认选项，保留上一个 PASS 网表。 | [v0.1 §1.2](PMUX优化技术方案_v0.1.md)、[RESULTS §6.4](../case_3_4/RESULTS.md) |
| 某候选只能靠 `module_name==test1` 才能找到两只 PMUX | P1：结构匹配 | P2：case1 达 182 | 该候选不得进入生产；先补双 PMUX 结构识别，P2 暂未通过。 | [case1 方法 §6](../case_1/CASE1_182LUT_特别详细方法说明.md) |
| 使用 case4 获胜分割向量常量查表，跳过结构搜索 | P1：任意输入统一算法 | P2：case4 达 26 | 禁止在运行时按 case 套用向量；以真值表/有界成本搜索产生分割，若无法达 26 则记缺口。 | [RESULTS §6.1–6.3](../case_3_4/RESULTS.md) |
| 一条通用清理规则使 case4 LUT 增加 | P2：四例各自不低于已验证目标 | P3：整体规则简化 | 默认关闭该规则或拒绝该候选；记录四例对照。 | [工程状态 §8](工程状态.md)、[v0.1 §8](PMUX优化技术方案_v0.1.md) |
| 直接将 case2 的 15 个原语与其它 case 的 `$lut` 数排序 | P2：同口径回归 | P3：统一成本选择 | 分开计数；补证已给行为级 25 `$lut`、显式 0 `$lut` + 15 原语，统一 Pass 最终 gate/EQY 仍待补；此前不宣称 P2 达标。 | [case2 补证](../case2_lut_opt_workspace/evidence_case2/RESULTS.md) |
| 结构代理分数下降，但真实 ABC9 不降或预算耗尽 | P0：证明完整，P2：真实成本门 | P3：快速候选选择 | 不提交，回退到上一候选/原网表。 | [v0.1 §0.6–0.7](PMUX优化技术方案_v0.1.md) |

## 9. 验证、命令与门禁

正式验证使用冻结官方 RTL 作为 gold，统一算法产出的对应最终优化网表为 gate，所有四例分别做 whole-design EQY。默认分区失败时允许保留完整日志后改用整设计 miter，但不得将“工具未完成”写成 PASS。[v0.1 §1.1、§7](PMUX优化技术方案_v0.1.md)、[M0 §8](M0三人协作启动检查清单.md)。

| 检查 | 已有状态与证据 | v0.2 必做门禁 |
|---|---|---|
| 输入与基线 | 本次 `competition_case/test1.v`～`test4.v` SHA-256 与 M0 一致；[工程状态 §3](工程状态.md)记录 288/25/13/36、222/0/17/10 DFF、0 IO。 | 统一固定 Yosys/EQY/ABC 版本，从官方 RTL 全流程重跑，保存命令、JSON、日志、EQY 配置与哈希。 |
| 结构与 `check` | v0.1 旧规则有结构检查；case1 [结构审计](../case_1/submit/structure/structure_audit.md)、[原型日志](../case_1/submit/logs/rtlil_prototype.log)已交，`check` 0 问题；ABC 警告保留见第 2.1 节。 | 每候选检查单驱动、选中范围、Y/端口/属性、时序边界、快照与原子提交；完整库环境中运行 `check`，不能只看进程返回码忽略内部告警。 |
| 19 微测试 | [工程状态 §7](工程状态.md)称旧 A/B 联调 19/19 PASS。 | 四类新规则各增正反例；旧 19/19 不可继承为新规则通过，必须重新跑并归档。 |
| 1000 固定种子 | [工程状态 §7](工程状态.md)称旧规则 1000/1000 EQY PASS。 | 新统一规则重新执行至少 1000 固定种子，保存失败种子；旧结果只作回归基准。 |
| 四官方 case | 旧生产 Pass：288/25/13/36 均 EQY PASS，无 LUT 收益；独立实验见第 3、7 节。 | 四例同一算法入口、同一流程、whole-design EQY PASS，逐例 LUT/DFF/IO、CPU/ABC 时间、峰值 RSS；逐例不低于可比的最好已知结果。 |
| case1 182 | [归档包](../case_1/submit/MANIFEST.md)322 哈希、阶段 RTLIL/JSON、结构审计、whole-design EQY PASS、CPU/RSS/sweep 已核验。 | 补精确工具链与可移植命令；生产规则 W=1/12/32、位序/别名和拒绝矩阵、新最终 gate EQY 与统一四例回归仍须通过。 |
| case2 15 原语 | [补证报告](../case2_lut_opt_workspace/evidence_case2/RESULTS.md)、[阶段统计](../case2_lut_opt_workspace/evidence_case2/stage_counts.csv)、[两条 EQY 证据](../case2_lut_opt_workspace/evidence_case2/MANIFEST.md)：行为级 25 `$lut`，显式 0 `$lut` + 15 原语，均 PASS。 | A/B 先确认显式原语在正式计分链合法；B 的统一 Pass 实际生成最终 gate 后，由 A 独立重计双口径并做新 EQY。 |
| case3 11 / case4 27（当前生产） | B `b8368b0...` 已由历史 A 授权和历史 C 独立验收；case4 旧 26 LUT 错误表候选继续拒绝。 | 后续生产变化由 B 提交，A 独立验收真实配置/gold/gate/PASS/日志/SHA256SUMS，并重新检查生命周期和固定种子。 |

推荐统一验收命令以工具路径参数化，不把固定 case 身份传给 Pass：

```sh
# 只做基线/最终验收；本版未执行这些长实验。
sha256sum competition_case/test{1,2,3,4}.v
<yosys> -V
<eqy> --version
<yosys-abc> -v
<baseline>/scripts/run_all.sh
<unified-regression> --seeds 1000 --micro all --official competition_case \
  --report out/v02-acceptance
# 具体统一回归脚本路径：MISSING；B 交生产入口、A 交独立验收入口后必须替换占位命令。
```

case2 已按[补证清单中的确定性命令](../case2_lut_opt_workspace/evidence_case2/MANIFEST.md)分别生成三阶段网表，并运行两条短 EQY 与 LUT4 自测；当前又有 B 的统一生产候选，但仍需 A 独立重跑和确认双口径。原 `run_official.py` 与 `run_eqy.ps1` 仅是旧短复核入口。case3/4 的历史搜索附件继续作为来源材料；正式结论以已验收生产提交为准。case1 原型附件仍保留原实验布局；生产候选不能以自动 cell 名或原绝对路径触发，A 应使用结构审计和独立重放确认。[来源与旧路径](../case_1/submit/PROVENANCE.md)、[结构脚本](../case_1/submit/structure/structure_audit.py)。

## 10. 阶段计划、A/B 双人分工与接口变更

本节中的 **A/B 是两名工作者**，ABC/ABC9 是逻辑映射工具。自 2026-10-24 起，不再按人员分配 case：B 对 case1～case4 的统一生产实现负责，A 对四类规则的证明边界、独立审计和最终验收负责。原 C 的独立验证、资源统计、EQY 和证据归档职责全部转交 A；历史 C 报告仍按原身份引用。[双人契约](G3双人协作与接口契约.md)。

### 10.1 四类规则的双人交付

| 规则族 / 验收 case | B：生产实现与集成 | A：证明、审计与独立验收 | 交接门禁 |
|---|---|---|---|
| 双 PMUX 重建 / case1 | 结构匹配、物理分支提取、18 MUX 重建、快照、原子提交及综合插入。 | 审核完整状态覆盖、比较扩展、S/B 位序、交叉关系、扇出和拒绝例；独立核验 182 LUT、222 DFF、whole-design EQY。 | B 交精确提交和自测；A 对该哈希给 `AUTHORIZED/BLOCKED`，再独立生成 gate。 |
| priority mux LUT4 链 / case2 | 识别任意合格宽度和链长，保持重叠选择优先语义，生成合法厂商原语并保守拒绝额外扇出。 | 审核链方向、端口位序、内部边界、LUT 掩码及正反例；独立分列 post-ABC9 `$lut` 和 `cycloneiv_lcell_comb` 并执行 EQY。 | 15 只能报告为原语数；A 未确认正式统计口径前不得改写为 15 `$lut`。 |
| guard 决策锥 / case3 | 维护生产消费者、快照预检、原子改写及默认路径。 | 审核 support 有序绑定、relation 集合/多重性、比较器唯一性和预算；独立核验 11 LUT/12 DFF、EQY。 | 任何证明绑定或消费者变化都需新的 A 授权。 |
| 常量表映射 / case4 | 维护表提取、快照、generation monitor、一次性授权、映射 profile 选择。 | 审核 32 行真值、生命周期、同名重建、伪造 generation 和重放反例；独立核验 27 LUT/10 DFF、EQY。 | 授权只绑定一次发布和精确目标生命周期；失败回退 stock。 |

### 10.2 横向工程职责与文件边界

| 工作者 | 独占主责 | 向对方交付 | 禁止越界 |
|---|---|---|---|
| A：证明与独立验收 | `analyzer/` 的证明实现和审计夹具；事实表、证明链、预算、原因码；独立微测试、1000 种子、四例统计/EQY、证据清单和 G3 判定。 | 向 B 冻结只读证明结果、守卫、结构义务、拒绝原因和可复现反例；对精确 B 哈希给书面结论。 | 不修改 `pass/`、生产 `techlibs/intel/` 或 B 候选；不降低 oracle；不得在审计中替 B 修复。 |
| B：生产 Pass 与集成 | `pass/`、生产 `synth_intel` 集成、统一入口、结构匹配、候选调度、快照预检、原子改写、成本门和机械合并。 | 向 A 交单一候选提交、修改清单、自测、阶段网表、原因码、计数和完整复现命令。 | 不复制证明规则进改写器，不放宽 A 的结果，不修改最终验收判据，不自行授权或宣告 G3。 |

`include/` 由 B 机械维护、A 批准字段语义；接口变化须先形成书面提案，A 确认后由 B 单独提交。独立验收脚本和 oracle 由 A 维护，B 的实现自测不能代替 A 验收。两人不得在同一工作树交替修改同一文件；跨边界变更拆分提交。[双人契约 §3](G3双人协作与接口契约.md)。

### 10.3 阶段、交付物和门禁

| 阶段 | 输入 | A / B 的具体动作 | 输出与停止条件 | 优先级 |
|---|---|---|---|---|
| M0 双人切换 | 固定提交、四输入哈希、工具提交和已有证据 | A/B 分别核对环境、远端、HEAD、工作树和输入；确认新文件边界。 | [双人切换清单](M0双人协作切换检查清单.md)全部满足；差异先停。 | P0 |
| 冻结规则和接口 | 四族结构谓词、反例和第 5.1 节计划字段 | A 冻结证明义务/预算/原因码和验收 oracle；B 冻结候选/快照/原子事务/插入点并复核接口可实现性。 | 双方书面同意优先语义、原语合法性、统计口径和接口；争议未清零不得默认启用。 | P0、P1 |
| B 生产候选 | 已归档附件和冻结接口 | B 在同一入口实现结构匹配及隔离改写，运行实现自测，交精确提交与证据；A 不参与修改。 | 无身份匹配；UNKNOWN/预算/冲突/快照失败零写入；单族官方 EQY 和成本可复现。 | P0、P1 |
| A 独立审计 | B 的完整提交哈希 | A 运行静态审计、行为变异、负例、预算、只读指纹和消费者检查；只报告，不修生产代码。 | `BLOCKED` 退 B 新提交；只有 `AUTHORIZED` 才进入最终验收。授权不继承后继提交。 | P0、P1 |
| A 最终验收 | 已授权的精确 B 提交 | A 独立生成 gate，运行微测试、1000 种子、四例 ABC9/原语/whole-design EQY，记录资源/时间/内存。 | 四例满足正式 oracle，DFF/IO 不回退，证据完整；失败退 B，不得自修。 | P0–P2 |
| 默认启用与合并 | G3 PASS 的精确提交 | B 做机械默认开关/合并；A 核对最终 diff 并做必要短回归。 | 合并内容与已验提交一致才可 release；否则重新审计。 | P0–P3 |
| P3 校准 | 已达标统一规则集 | B 调整成本/映射候选；A 审核证明边界并重跑完整门禁。 | 不满足 P0/P1/P2 的调参一律拒绝。 | P3 |

G3 的七项技术条件不因人数缩减而变化：微测试全部通过、1000 固定种子 EQY、四例 EQY、四例 LUT 无增加且至少一例降低、DFF/IO 不增加、CPU/峰值内存完整、无名称/源码位置硬编码。双人制另外要求 B 不自验、A 不修生产代码、授权绑定精确提交以及最终 gate 由 A 独立生成。[双人契约 §7](G3双人协作与接口契约.md)。

## 11. 风险、缺失证据与明确的未决项

| 项目 | 当前状态 | 所需证据或决策 |
|---|---|---|
| 工具链 | case2 所用 Yosys/ABC/EQY 版本、源码完整提交和二进制 SHA-256 已见[工具链记录](../case2_lut_opt_workspace/evidence_case2/toolchain.txt)；历史原实验工具与正式冻结工具是否逐项相同仍需按证据判断。 | A/B 使用同一固定工具链重放四例；B 记录构建，A 独立记录验收工具与可执行哈希。 |
| case1 | 原型和历史证据已归档；B 通用生产候选 `504df2a...` 已推送。 | A 审核结构匹配、位序、快照和拒绝例，独立重跑 182 LUT/222 DFF/EQY；完成前为 `PENDING_A_AUDIT`。 |
| case2 | 历史双口径证据已补；B 已有通用生产候选 `504df2a...`。 | A 确认厂商原语在正式计分链合法，独立分列 post-ABC9 `$lut`、15 个原语、DFF/IO 和 EQY；不得把原语数写成 `$lut` 数。 |
| case3 | 旧重建包仍缺原始实验产物；正式生产路径另行完成并在 `b8368b0...` 上验收为 11 LUT/12 DFF。 | 后续由 B 交新候选，A 对实际完整 gate、证明绑定、DFF 边界和 EQY 独立复验。 |
| case4 | 旧重建附件中的错误表仍是被拒绝的历史候选；正式生产路径已在 `b8368b0...` 上完成原 A/C 验收，为 27 LUT/10 DFF。 | 后续 B 变更必须重新交 A；A 独立核验 32 行真值、generation/lifecycle/one-shot 反例、whole-design EQY 和计数。旧错误附件不得重新启用。 |
| 统一算法 | 同时达到 182/15 原语/11/26 的生产 Pass、四例一次性报告：`MISSING`。 | 先实现统一结构规则和原子门，后执行四例 P0/P1/P2 统一验收。 |
| 映射稳定性 | case1 报告不同拓扑为 182–321；case2 行为网表被 ABC9 映回 25；case4 需特定 ABC9 搜索脚本。 | 保存生成顺序、阶段网表和脚本；对四例做同口径成本门，防止局部收益造成其它例回退。 |
| 新附件可信边界 | case1 日志有 ABC 内部警告，成功返回码/PASS 不意味着没有警告；case3/4 包无有效哈希清单且全部重建，当前 26 标签不是统计证据。 | 保留原日志和状态陈述；禁止把文件存在、文件名数字、重建来源或模板当作已执行验收。第 2.1–2.2 节给出核验路径和反例。 |

特别注意 `$pmux` 与 priority `$mux` 的语义边界：前者在多选时可为 X，后者有确定优先级。v0.1 R1′只替换被选数据位；v0.2 的**删除、合并或重建整分支**必须另外证明选择互斥，或证明所用优先链的精确语义，不能把某个技术映射实现的重叠处理当作 `$pmux` 契约。[v0.1 §3、§6](PMUX优化技术方案_v0.1.md)、[case1 方法 §6](../case_1/CASE1_182LUT_特别详细方法说明.md)、[case2 EQY gold RTLIL](../case2_lut_opt_workspace/official_eqy/gold.il)。

## 12. v0.1 → v0.2 变更记录

| 修改项 | 来源依据 | 影响范围 | 是否影响 EQY | 是否影响四例最好已知目标 | 是否已实现 |
|---|---|---|---|---|---|
| 增加 P0>P1>P2>P3，分离运行时结构规则和验收 oracle | 本版任务约束、[v0.1](PMUX优化技术方案_v0.1.md) | 总体准入与报告 | 强制保留 P0 | 明确 P2 但当前未达标 | 文档已定义；统一实现 `MISSING` |
| 双 PMUX 共享状态重建 | [case1 方法](../case_1/CASE1_182LUT_特别详细方法说明.md)、[归档](../case_1/submit/MANIFEST.md)、B `504df2a...` | 新增 18 MUX 的候选网络 | B 自测 EQY PASS；待 A 独立重验 | case1 目标 182 `$lut` | 生产候选已实现；`PENDING_A_AUDIT` |
| priority mux LUT4 链和显式原语映射 | [case2 方法](../case2_lut_opt_workspace/OPTIMIZATION_ALGORITHM_AND_REPRODUCTION.md)、[补证](../case2_lut_opt_workspace/evidence_case2/RESULTS.md)、B `504df2a...` | 工艺相关映射 | B 自测 EQY PASS；待 A 独立重验和口径确认 | case2 15 原语；不得写成 15 `$lut` | 生产候选已实现；`PENDING_A_AUDIT` |
| guard 下比较缩宽、数据折叠与决策重建 | [RESULTS §5](../case_3_4/RESULTS.md)、正式 B `b8368b0...` 和历史 C `23ef422...` | case3 类受控选择锥 | 正式生产 gate whole-design EQY PASS | case3 当前 11 LUT/12 DFF | 已生产并验收；后续修改须重新审计 |
| 有界常量表 cofactor 与共享映射 | [RESULTS §6](../case_3_4/RESULTS.md)、正式 B `b8368b0...` 和历史 C `23ef422...` | memory mapping 后表/选择锥 | 正式生产 gate whole-design EQY PASS；旧错误表继续拒绝 | case4 当前 27 LUT/10 DFF；历史 26 未闭合 | 已生产并验收；后续修改须重新审计 |
| 统一证明/成本/EQY/原子回退协议 | [双人 G3 契约](G3双人协作与接口契约.md)、[工程状态](工程状态.md) | 四规则集成 | 新规则均须 whole-design EQY | 防四例回退 | case3/4 已验收；case1/2 候选待 A 审计，整体仍未最终合并 |
| 双统计口径和证据等级 | [case1 方法](../case_1/CASE1_182LUT_特别详细方法说明.md)、[case2 方法](../case2_lut_opt_workspace/OPTIMIZATION_ALGORITHM_AND_REPRODUCTION.md)、[RESULTS](../case_3_4/RESULTS.md) | LUT、原语、LUT6 报告 | 不改变逻辑；暴露缺证据 | 防止异口径误判 P2 | 本文已定义；统一报告 `MISSING` |
| 三人制切换为 A/B 双人制 | 第 10 节、[双人契约](G3双人协作与接口契约.md)、[切换清单](M0双人协作切换检查清单.md) | A 承接证明、审计和原 C 验收；B 负责四族全部生产实现 | 保留独立验收，禁止作者自批 | 不改变四例 oracle；只改变职责切面 | 文档已定义；case1/2 等待 A 审计 |
| 新附件审计与拒绝原因 | 第 2.1–2.2 节，case1 submit、case3/4 双层 submit | 来源、哈希、统计阶段、真实证明对象与交接要求 | case1 归档 PASS 可追溯；case4 给出明确反例，禁止启用 | 不修改验收目标；区分历史目标、归档已验和被反驳重建候选 | 本轮仅文档/只读核验；未修改附件、代码或测试，未重跑实验 |

**本版结论：** 四类算法均可表述为结构规则族，P1 与 P2 在设计上兼容。case3/4 的生产路径已有历史 G3 PASS；case1/2 已有 B 生产候选，但尚未取得双人制下 A 对精确提交的独立授权和四例统一最终验收，因此整体 release/P2 仍不得宣告完成。今后的唯一职责链是：B 生产候选与修复，A 证明审计与独立验收；原 C 不再是新提交的必要参与者，同时也不允许 B 自验替代 C。人员变化不修改官方输入、冻结接口语义、测试期望或七项 G3 技术门槛。[工程状态](工程状态.md)、[双人契约](G3双人协作与接口契约.md)、[切换清单](M0双人协作切换检查清单.md)。
