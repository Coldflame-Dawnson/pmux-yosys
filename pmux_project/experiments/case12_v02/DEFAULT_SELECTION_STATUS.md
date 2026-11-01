# 四规则统一默认入口：B 交付与 A 复审边界

日期：2026-11-01。状态：`READY_FOR_A_DEFAULT_PATH_AUDIT`，尚非新提交的 A 授权或 G3 结论。

起点是已获 A 授权的 B 提交 `4c43ab9743d69ee6fe28f965e10163b15bdb5c88`。
A 授权提交为 `d6e9738e1e37f028583941085cd71f2b85b6fa47`，位于
`feature/a-v02-case12-width-reaudit-4c43ab9`。本轮工作分支为
`feature/b-v02-unified-case12-default`，不合入主分支或 `integration/g3`。

## 1. 集成行为

普通 `pmux_opt` 现在按固定顺序执行已授权的 paired-PMUX、四分支 priority 链，
然后执行既有 decision-cone/constant-table 规则和通用局部位分析。规则只看结构；
case12 的证明、快照、原子改写和整批畸形宽度拒绝均沿用起点版本。

| 入口 | 正常默认 | 单独回退 case12 | 完整结构回退 |
|---|---|---|---|
| Pass | `pmux_opt` | `-no-v02-case12` | `-no-v02-case12 -no-v02-case34` |
| 原生 synth | `synth_intel -family cycloneiv` | `-no-pmux-v02-case12` | `-no-pmux-v02` |
| Python runner | 不添加模式参数 | `--no-v02-case12` | `--no-pmux-v02` |

`-v02-case12` 和 `-pmux-v02-case12` 保留为兼容参数；同族 enable/disable 参数冲突时
在改写前报错。stub 与 decision-proof mutation 测试模式隔离结构改写。
`-no-v02-case34` / `--no-v02-case34` 仅关闭 case34，不能再当作四规则完整基线开关。

priority 规则直接产生 `cycloneiv_lcell_comb`，因此 synth 与 runner 在其他目标系列
默认关闭 case12；显式要求其他系列启用 case12 时原生 synth 拒绝。直接调用 Pass 的
调用者负责选择 Cyclone IV 工艺环境。这里的系列判断是工艺边界，不读取设计身份。

`run_production_validation.py` 保持历史 case34 专项 oracle，并显式关闭 case12，
用于隔离专项回归。四规则默认验收使用本文的新自测驱动和 A 的独立驱动。

## 2. B 自测结果与口径

| case | 默认 post-ABC9 `$lut` | 同阶段 `cycloneiv_lcell_comb` | DFF | profile | 新 gate whole-design EQY |
|---|---:|---:|---:|---|---|
| 1 | 182 | 0 | 222 | stock | PASS |
| 2 | 0 | 15 | 0 | stock | PASS |
| 3 | 11 | 0 | 12 | stock | PASS |
| 4 | 27 | 0 | 10 | accepted_constant_table | PASS |

case2 只能报告为 15 个厂商原语。厂商 `map_cells` 后计数另存 `summary.json` 的
`official[].variants.*.vendor_counts`，不得混用两个统计阶段。

- 默认/显式 case12 结构夹具分别 8/8 PASS。
- 新默认选择自测 47/47 PASS：双路径默认/完整回退、单族回退、非目标系列、冲突参数、
  stub 隔离、畸形宽度整批零改写、四例新 gate EQY。
- 每例默认和显式原生 gate SHA-256 完全一致。
- 完整回退恢复 288/25/13/36 `$lut` 和 222/0/17/10 DFF；仅关闭 case12 时保留
  case3 11/12、case4 27/10。未请求 IO pad 插入。
- 通用微测试 19/19 PASS；本轮默认模式真实固定种子自测 100/100 PASS。
- Analyzer fast：2116/29/60/50/81 PASS；production mutation seam：26/26 PASS。
- 授权快照/generation/lifecycle 双路径矩阵：14/14 PASS；见证据 `profile14/profile_snapshot_results.json`。
- 本轮没有重跑未变证明引擎的 slow 测试，也没有用旧 1000+1000 结果替代本提交的
  A 独立 1000 种子和默认路径最终验收。

## 3. 复现命令

以下变量由接收环境设置；构建与实验在 Linux 文件系统中的本仓库进行。

```bash
REPO=/absolute/path/to/pmux-yosys
TOOL="$REPO/pmux_project/official_baseline/toolchain/install"
OFFICIAL="$REPO/pmux_project/pmux_case/competition_case"
cd "$REPO"
make -C pmux_project/pass all YOSYS_CONFIG="$TOOL/bin/yosys-config"
python3 pmux_project/experiments/case12_v02/scripts/run_case12_validation.py \
  --yosys "$TOOL/bin/yosys"
python3 pmux_project/experiments/case12_v02/scripts/run_case12_validation.py \
  --default --yosys "$TOOL/bin/yosys"
python3 pmux_project/experiments/case12_v02/scripts/run_default_selection_validation.py \
  --tool-root "$TOOL" --official "$OFFICIAL" \
  --out "$REPO/pmux_project/reports/b-case12-default-selection-final-delivery" --eqy
python3 pmux_project/experiments/case34_v02b/scripts/run_profile_snapshot_tests.py \
  --tool-root "$TOOL" --official-dir "$OFFICIAL" \
  --output "$REPO/pmux_project/reports/b-case12-default-profile14"
```

原生自测使用 `native_synth_loader.cc` 将当前真实 `synth_intel.cc` 装入冻结 Yosys 0.69，
不是伪造综合流程。Python runner 独立使用同一个生产 Pass。驱动记录完整命令、输入哈希、
阶段 IL/JSON、profile、EQY 配置/日志与 PASS。A 应自行构建并重新生成 gate。

## 4. 证据与披露

完整本机报告位于 `pmux_project/reports/b-case12-default-*`。可提交证据包位于
`default_selection_evidence/`，含逐文件清单和压缩包 SHA-256。包排除工具二进制和官方 RTL。

首次自测使用另一种模型准备方式并返回 EQY FAIL，日志与原配置保存在证据包
`initial_formal_config_failure/`。之后采用既有已验证的公共准备流程
`hierarchy -check; prep; memory_map; async2sync` 和 whole-design merged partition，
四例 EQY 均 PASS。两次使用同一官方 gold 和同一候选逻辑；没有修改 RTL、冻结证明接口、
oracle 或生产改写规则来消除失败。A 应独立核对正式配置和完整 gate。

对刻意畸形的 `$eq` 宽度负例仍采用已审计的零写入断言与测试专用 `design -reset`，
不在非法设计上调用会自身崩溃的 Yosys `check/stat`。正常结构继续运行结构检查。

本提交只交付 B 的默认集成实现与自测。新默认策略须由 A 对精确新哈希给出独立结论，
现有授权不自动继承，也不由 B 自行宣告 G3 或 release 完成。
