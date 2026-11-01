# case3/case4 v0.2-B 可复现证据包

本目录由 B 从官方 `pmux_project/pmux_case/competition_case/test3.v`、
`test4.v` 重新构造，用于闭合算法语义、严格可比的 post-ABC9 统计和
whole-design EQY。它不是生产实现；生产规则不得读取 case 名、模块名、
文件名或固定获胜向量作为匹配条件。

## 内容

- `scripts/case3_guard_candidate.v`：保持完整时序与未更新位语义的候选。
- `scripts/generate_case4_candidate.py`：从官方拼接 RHS 提取 32×10 真值表，
  生成 Shannon/cofactor 候选；不含手写表。
- `scripts/run_short_validation.py`：运行两例基线、候选、EQY 和厂商映射。
- `scripts/search_case4_splits.py`：固定种子、有界候选和贪心邻域搜索。
- `evidence/`：本次执行生成的 IL、JSON、日志、EQY 配置和 PASS 文件。
- `search/`：544 个唯一候选的完整轨迹及获胜候选。
- `RESULTS.md`：算法、统计口径、结果及差异结论。
- `MANIFEST.md`、`SHA256SUMS`：附件用途、依赖关系和完整性校验。

## 重放

从 Ubuntu 24.04 WSL 的仓库根目录运行：

```bash
TOOL=/home/james/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/bin
OUT=/tmp/pmux-case34-short
python3 pmux_project/experiments/case34_v02b/scripts/run_short_validation.py \
  --repo /home/james/work/pmux-yosys \
  --workspace "$PWD/pmux_project/experiments/case34_v02b/scripts" \
  --out "$OUT"

SEARCH=/tmp/pmux-case4-search
python3 pmux_project/experiments/case34_v02b/scripts/search_case4_splits.py \
  --official pmux_project/pmux_case/competition_case/test4.v \
  --yosys "$TOOL/yosys" \
  --lut-map pmux_project/experiments/case34_v02b/scripts/case4_lut4_map.v \
  --out "$SEARCH" --random-count 500 --rounds 4
```

重放会写新目录，不覆盖归档证据。归档中的绝对路径记录原执行位置，是审计
信息，不是运行时匹配条件。
