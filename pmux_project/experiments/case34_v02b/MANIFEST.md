# case3/case4 v0.2-B 附件清单

## 来源与边界

- 官方 RTL 不在本目录重复提交；运行时从
  `pmux_project/pmux_case/competition_case/test3.v`、`test4.v` 读取。
- `scripts/` 是 B 新写的可重放算法/验证材料。
- `evidence/` 和 `search/` 是 2026-10-07 使用 `toolchain.txt` 所列固定工具实际
  生成的归档产物。
- 所有文件的 SHA-256 见 `SHA256SUMS`；该文件由
  `scripts/update_sha256s.py` 确定性生成。
- 本包不含生产 Pass 改写，也不修改官方输入、冻结接口或测试期望。

## 关键附件

| 路径 | 用途 | 直接依赖/生成方式 |
|---|---|---|
| `scripts/case3_guard_candidate.v` | case3 完整时序候选 | 官方 `test3.v` 语义与 guard 规则 |
| `scripts/generate_case4_candidate.py` | 官方拼接表提取和 cofactor 候选生成 | 官方 `test4.v` |
| `scripts/case4_lut4_map.v` | cofactor 内部 cell 到 LUT4 的确定性 lowering | Yosys techmap |
| `scripts/run_short_validation.py` | 基线/候选、两级 EQY、厂商映射 | 固定工具链和官方 RTL |
| `scripts/search_case4_splits.py` | 固定种子 544 候选有界搜索 | 正确官方表、固定 Yosys |
| `evidence/summary.json` | 全部短验证返回码、计数、用时和哈希 | `run_short_validation.py` |
| `evidence/generated/case4_truth_table.json` | 32×10 官方表 | 生成器自动解析，不是手写表 |
| `evidence/generated/case4_cofactors.json` | 获胜向量和 cofactor key | 生成器 |
| `evidence/*/post_abc9.{il,json}` | 严格统计和 gate EQY 对象 | 对应 `run.ys`/`yosys.log` |
| `evidence/*_eqy/*.eqy`、`eqy.log`、`PASS` | 实际 EQY 配置、日志和成功标记 | source 或实际 gate |
| `evidence/*_vendor/vendor.{il,json}` | 厂商原语独立口径 | Intel `cells_map.v` |
| `search/trace.jsonl` | 544 个唯一候选完整轨迹 | 固定种子 `20261006` |
| `search/summary.json` | 最佳向量及贪心停止原因 | 搜索脚本 |
| `search/best/` | 最佳候选和严格 post-ABC9 产物 | 搜索中严格改善时保存 |
| `RESULTS.md` | 算法、结果、口径差异和生产门禁 | 上述全部证据 |
| `toolchain.txt` | 工具版本、提交、二进制哈希和环境 | 固定工具实际输出 |

## 已知未决项

- C 的独立重放和验收意见：`MISSING`。
- A 的 case3 guard/比较器与 case4 真值表证明接口：`MISSING`。
- 统一生产 Pass、19 微测试、1000 固定种子和四例统一回归：`MISSING`。
- 与严格完整流程同阶段的 case4 26 `$lut` 证据：`MISSING`；当前可信结果是
  27 `$lut`，厂商口径为 26 个 `fiftyfivenm_lcell_comb` 加 1 个 `$not`。
