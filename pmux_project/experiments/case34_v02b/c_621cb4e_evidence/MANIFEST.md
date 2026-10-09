# C evidence manifest

状态：BLOCKED；唯一已确认阻塞为授权后 marker LUT 参数变更仍选择 accepted_constant_table。

Git 中保留报告、C 测试脚本/夹具和小型摘要；完整逐文件机器清单 artifact_inventory.jsonl 保存在 Git 忽略报告目录，其 SHA-256 由 archive_summary.json 和 SHA256SUMS 绑定。机器清单为每个大型生成物列出用途、生成命令、输入依赖、哈希和 Git=false。所有未执行结论是 UNVERIFIED；下列预期结果文件未生成，为 MISSING。

| 预期材料 | 状态 |
|---|---|
| full default seed 1000 aggregate PASS | MISSING / UNVERIFIED（按用户要求在生产缺陷出现后停止） |
| explicit seed 1000 aggregate PASS | MISSING / UNVERIFIED（按用户要求在生产缺陷出现后停止） |
| production mutation 26 results | MISSING / UNVERIFIED（按用户要求在生产缺陷出现后停止） |
| unified behavior 17 results | MISSING / UNVERIFIED（按用户要求在生产缺陷出现后停止） |
| Analyzer fast/slow results | MISSING / UNVERIFIED（按用户要求在生产缺陷出现后停止） |
| regenerated-cell replay logs | MISSING / UNVERIFIED（按用户要求在生产缺陷出现后停止） |
| read-only/determinism/table32 independent results | MISSING / UNVERIFIED（按用户要求在生产缺陷出现后停止） |

## Git 跟踪文件

| 文件 | 用途 | 生成命令 | 输入依赖 | SHA-256 | Git |
|---|---|---|---|---|---|
| pmux_project/experiments/case34_v02b/C_DEFAULT_PATH_FINAL_ACCEPTANCE_621cb4e.md | C 最终 BLOCKED 验收报告 | finish_report.py | 本轮 native/反例/微测试/停止回归证据 | `555e959a4f49156203a3734ef51a66cd3729cbcdf52d776ce1e63af46286d2bf` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/a_authorization_report.md | A 只读授权材料，非 C 判定 | git show 19b8dee...:pmux_project/analyzer/unified_selection_consume_reaudit_621cb4e.md | A HEAD 19b8dee... | `4b06ab59d5c541d5c189dacc71af5785963193322d72e389fbfa92b4783dab0a` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/archive_c.py | C 验收/归档脚本或测试夹具，非生产实现 | C 创建；编译/执行见 commands.txt 和完整报告 | 固定 B 源码、冻结工具/测试接口与本轮任务要求 | `eb09691f72b3cb548245339227df42e502c7816ea95b6f7142dfd4e12ff127a9` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/archive_summary.json | 本轮环境/资源/profile/回归/归档摘要 | build_c.py / native_acceptance.py / archive_c.py / create_manifest.py | 本轮原始日志和固定输入；详见完整报告 | `152c27786e637782cfc83c5a957cbec01e541f33710360a5e3daf89b48009897` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/build_c.py | C 验收/归档脚本或测试夹具，非生产实现 | C 创建；编译/执行见 commands.txt 和完整报告 | 固定 B 源码、冻结工具/测试接口与本轮任务要求 | `5d30fa009cf2b28fa5893562b5895548b5fd9ff805a8ead3a5c95eee67b8c0ac` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/commands.txt | 本轮环境/资源/profile/回归/归档摘要 | build_c.py / native_acceptance.py / archive_c.py / create_manifest.py | 本轮原始日志和固定输入；详见完整报告 | `43fb6cb13068f00d40d62da3ac1f069a565183436fcc75d030313949d098c303` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/counterexample_assertions.txt | 最小单参数位变更反例配置/摘要 | snapshot_counterexample.py + archive_c.py | C native 故障注入运行及冻结 EQY 配置 | `edcc7d03de7f12a821cda32b9773a0b336e3c5f491cde322aa21f4b40b8586a9` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/counterexample_one_bit.diff | 最小单参数位变更反例配置/摘要 | snapshot_counterexample.py + archive_c.py | C native 故障注入运行及冻结 EQY 配置 | `6da9fb2182418a029b02b81988baffe3cbad32804f9ba00f42a5cc0c745889c1` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/counterexample_plan.json | 最小单参数位变更反例配置/摘要 | snapshot_counterexample.py + archive_c.py | C native 故障注入运行及冻结 EQY 配置 | `55ee922805275384faab1312e2282aa2ea7ccd4e4c8235929d11457738413d12` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/counterexample_run.ys | 最小单参数位变更反例配置/摘要 | snapshot_counterexample.py + archive_c.py | C native 故障注入运行及冻结 EQY 配置 | `dc5b10178e70f714e6108a1d23dcc1aa1e3002370429b14d78f0d4d39526c88a` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/counterexample_stats.json | 最小单参数位变更反例配置/摘要 | snapshot_counterexample.py + archive_c.py | C native 故障注入运行及冻结 EQY 配置 | `4c02167d421fdf08c141d896f341b0a1c20540b0497abd09272c04ad29e96daa` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/counterexample_verify.eqy | 最小单参数位变更反例配置/摘要 | snapshot_counterexample.py + archive_c.py | C native 故障注入运行及冻结 EQY 配置 | `2fd14d84a8731303c03eae6444a0786e6ee5077fd43fe84e8bf3878814eb2e9e` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/create_manifest.py | C 验收/归档脚本或测试夹具，非生产实现 | C 创建；编译/执行见 commands.txt 和完整报告 | 固定 B 源码、冻结工具/测试接口与本轮任务要求 | `0e036eada1558ba64f865236191c4496593cd6010695ee82da04efab9afde4a0` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/dut_adapter.py | C 验收/归档脚本或测试夹具，非生产实现 | C 创建；编译/执行见 commands.txt 和完整报告 | 固定 B 源码、冻结工具/测试接口与本轮任务要求 | `833e786ca3a04b399a988239f80a824386a80af9ab73381ff424453914fab743` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/finish_report.py | C 验收/归档脚本或测试夹具，非生产实现 | C 创建；编译/执行见 commands.txt 和完整报告 | 固定 B 源码、冻结工具/测试接口与本轮任务要求 | `5ba01fef3f7b13ff3f9788f54ddf2d3987762cb929fe94e4c1a6b03366affd2b` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/initial_hashes.json | 本轮环境/资源/profile/回归/归档摘要 | build_c.py / native_acceptance.py / archive_c.py / create_manifest.py | 本轮原始日志和固定输入；详见完整报告 | `33ce89dd627e5081724169a2cab967e3fc402711faa0a1606c084ec972ee32fd` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/micro_summary.json | 本轮环境/资源/profile/回归/归档摘要 | build_c.py / native_acceptance.py / archive_c.py / create_manifest.py | 本轮原始日志和固定输入；详见完整报告 | `ad2ce87046155d4183f53b1817aac50370cb1d2459a9184c7b57cab47686bfea` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/native_acceptance.py | C 验收/归档脚本或测试夹具，非生产实现 | C 创建；编译/执行见 commands.txt 和完整报告 | 固定 B 源码、冻结工具/测试接口与本轮任务要求 | `b52d6a5bb9b9f55c514da397b1776527bc20e774a24f2a716def03b818ee128a` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/native_results.json | 本轮环境/资源/profile/回归/归档摘要 | build_c.py / native_acceptance.py / archive_c.py / create_manifest.py | 本轮原始日志和固定输入；详见完整报告 | `b2c80002edbce18b6a4a67f5091849ce87f054b2c92ee5c1c18ddba6fb0f9914` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/native_synth_loader.cc | C 验收/归档脚本或测试夹具，非生产实现 | C 创建；编译/执行见 commands.txt 和完整报告 | 固定 B 源码、冻结工具/测试接口与本轮任务要求 | `5afda1699547e71e8102b50ed1be53575da03642f0da3ecfe4fc31904fdddf04` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/official_input_comparison.json | 本轮环境/资源/profile/回归/归档摘要 | build_c.py / native_acceptance.py / archive_c.py / create_manifest.py | 本轮原始日志和固定输入；详见完整报告 | `36814f91fcafbd42b00ffdc94056f863f60b0483837ddac4cb01c661055b4af4` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/producer_observer.cc | C 验收/归档脚本或测试夹具，非生产实现 | C 创建；编译/执行见 commands.txt 和完整报告 | 固定 B 源码、冻结工具/测试接口与本轮任务要求 | `cf590df24fcccd008f3e0b58b68111ca9402622d0a956fad0c453e77a3689dad` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/profile_selection.csv | 本轮环境/资源/profile/回归/归档摘要 | build_c.py / native_acceptance.py / archive_c.py / create_manifest.py | 本轮原始日志和固定输入；详见完整报告 | `980d05ff2921e8c1da7a9ba55ecc8c01c2661bc613edb3d80bf9da88d6b71598` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/provenance.json | 本轮环境/资源/profile/回归/归档摘要 | build_c.py / native_acceptance.py / archive_c.py / create_manifest.py | 本轮原始日志和固定输入；详见完整报告 | `387f5e99c90912ac31b6977d7b7baea78f3a37b29af755f9cb532259c84abe65` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/regress_c.py | C 验收/归档脚本或测试夹具，非生产实现 | C 创建；编译/执行见 commands.txt 和完整报告 | 固定 B 源码、冻结工具/测试接口与本轮任务要求 | `14dceb286502727fe97bbf62d4a88e77d9974e86880697d902129ac5a7ccabe1` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/regression_stop.json | 本轮环境/资源/profile/回归/归档摘要 | build_c.py / native_acceptance.py / archive_c.py / create_manifest.py | 本轮原始日志和固定输入；详见完整报告 | `2a041d31dc065c26fbb2ea19fd2f7379d4dfd5fa7296b4d1a4c08536d987d291` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/regression_summary.txt | 本轮环境/资源/profile/回归/归档摘要 | build_c.py / native_acceptance.py / archive_c.py / create_manifest.py | 本轮原始日志和固定输入；详见完整报告 | `902efebf74427f682b098e4c84192d670d3f60747d43414bce3d04ed8518dace` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/replay_test_summary.txt | 本轮环境/资源/profile/回归/归档摘要 | build_c.py / native_acceptance.py / archive_c.py / create_manifest.py | 本轮原始日志和固定输入；详见完整报告 | `a785074cbf2657defa390462111915e344fec39435557d9d4e2eb658a003f64f` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/results.csv | 本轮环境/资源/profile/回归/归档摘要 | build_c.py / native_acceptance.py / archive_c.py / create_manifest.py | 本轮原始日志和固定输入；详见完整报告 | `31b84080b78dda0243f206b3d8d698d1ad6742111963b43054d8a7bacbcd80d8` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/snapshot_counterexample.json | 最小单参数位变更反例配置/摘要 | snapshot_counterexample.py + archive_c.py | C native 故障注入运行及冻结 EQY 配置 | `478d39db285821c1b36001d11dd060319f43b6b1998d296fcec638ba90930cd7` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/snapshot_counterexample.py | C 验收/归档脚本或测试夹具，非生产实现 | C 创建；编译/执行见 commands.txt 和完整报告 | 固定 B 源码、冻结工具/测试接口与本轮任务要求 | `8c7d51cf9a8d7156d322e2d6f96504fb3ec24a2f8a950ec92b5f50e3640a521f` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/stop_on_defect.py | C 验收/归档脚本或测试夹具，非生产实现 | C 创建；编译/执行见 commands.txt 和完整报告 | 固定 B 源码、冻结工具/测试接口与本轮任务要求 | `7b70752aafc529ff07d2512a2ef9591ee508ef41a5bb31fff43860c7141b4934` | yes |
| pmux_project/experiments/case34_v02b/c_621cb4e_evidence/toolchain.txt | 本轮环境/资源/profile/回归/归档摘要 | build_c.py / native_acceptance.py / archive_c.py / create_manifest.py | 本轮原始日志和固定输入；详见完整报告 | `905a5956df25034f027c1265553660ce453dcf0b7ed8cb0644ea4482f7691275` | yes |

## 大型生成物

- 文件数：45884；字节数：364547429。
- 逐文件 inventory：`pmux_project/reports/c-621cb4e-default-final/artifact_inventory.jsonl`。
- inventory SHA-256：`03071892c40579043e773762d781cd8164c9001044b09ebed6125b0b304733cf`。
- native/case[1-4]/baseline 与 default：post-ABC9 JSON/RTLIL、最终 RTLIL、完整日志、run.ys、CPU/RSS。
- native/case[1-4]/eqy：冻结配置、完整日志、工作目录 PASS 标记。
- snapshot_counterexample：真实 producer 与变更后的阶段 RTLIL、映射 gate、完整 EQY 工作目录，EQY 总状态 ERROR；正式不等价 UNVERIFIED。
- random/default：582 个完成的种子，以及停止时未完成样本；random/explicit 未执行。
- MANIFEST.md 和 SHA256SUMS 的自哈希不可自包含；MANIFEST.md 由 SHA256SUMS 绑定，SHA256SUMS 由最终 Git commit 绑定。modified_files.txt 为文件名索引，Git commit 绑定。
