# 中科亿海微选题二：四个官方 PMUX case 原版基线

本目录只包含可复现的基线流程；没有修改官方 RTL，没有加载 `pmux_local.so` 或任何自定义插件，没有调用 `pmux_opt`，也没有使用 eLinx。综合固定为原版 Yosys 0.69 的：

```text
synth_intel -family cycloneiv
```

## 快速运行

在 WSL2 Ubuntu 24.04 的干净终端中：

```bash
cd /mnt/d/Work/Embedded_competition/Original_results
official_baseline/scripts/build_toolchain.sh   # 首次：独立构建/安装工具；后续为版本检查后快速退出
official_baseline/scripts/run_all.sh
```

`run_all.sh` 不进行网络下载或工具编译。任一完整综合、后-ABC9 RTLIL 生成、EQY 或一致性检查失败都会返回非零。`test_failure_propagation.sh` 还会用非法 case 名做负向检查，确认脚本不会将失败误报为成功。

## 输入与工具

- 输入、顶层和依赖检查见 `environment/input_inventory.md`。采用的四个 RTL 是工作区父目录下的 `pmux_case/competition_case/test1.v` 至 `test4.v`；每案仅一个 Verilog 文件。
- 工具版本、路径、编译器、WSL/Ubuntu、ABC 版本/commit、EQY/SBY 版本与求解器情况见 `environment/toolchain.txt`。
- `build_toolchain.sh` 固定 Yosys 为 tag `v0.69` / commit `9f75ca1f9834a39a863915b5dae0c7b1e33533bc`，EQY 为 `v0.69`，SBY 为 `v0.69`，且安装到本目录下的 `toolchain/install`，不会接触已有 0.68 或 OSS CAD Suite。
- 最小 `synth_intel`/ABC9 冒烟测试的命令、完整日志、RTLIL 和返回码在 `environment/minimal_abc9_smoke.*`。

备注：官方 GitHub `v0.69` release tarball 曾显示为 `0.69+post`（其打包时嵌入的 `.gitcommit` 与 tag 不同）。本基线因此以官方 tag 的精确 commit 为准，而不用该 release asset 的版本字符串；相关下载件仅作该口径差异的留存，未参与任何综合。

## ABC9 截点

已运行 `yosys -p "help synth_intel"`，并读取 `techlibs/intel/synth_intel.cc` 的对应实现，记录在：

- `environment/synth_intel_help.log`
- `environment/synth_intel_source_excerpt.txt`

Yosys 0.69 的阶段标签为 `begin`、`family`、`coarse`、`map_bram`、`map_ffram`、`map_ffs`、`map_luts`、`map_cells`、`check`、`vqm`、`vpr`。`map_luts` 内执行 `abc9 -lut 4 -W 300`，后面才是 `map_cells` 的 Cyclone IV 厂商单元映射。因此后-ABC9、前厂商组合单元映射的网表命令是：

```text
synth_intel -family cycloneiv -top <top> -run begin:map_cells
```

每案保存的 `netlists/post_abc9.il` 都会由脚本检查至少包含一个 `cell $lut`。完整综合另行执行，不用该截点替代正式综合。

## EQY

Gold 是原始 RTL；Gate 是 `post_abc9.il`。EQY 先试默认分区，只有默认分区失败时才运行 `[partition <top>] merge *` 的整设计 miter。两个尝试的配置、工作目录、命令和完整日志均保留在各 `caseN/eqy/`。

后-ABC9 Gate 中的触发器为 `dffeas`。`environment/dffeas_formal.v` 仅建模 Yosys `intel/common/ff_map.v` 实际产生的子集；每案由 `analyze_netlist.py --validate-dffeas` 检查 `prn=1`、`asdata/aload/sclr/sload=0`，否则流程失败。`memory_map` 仅用于 Gold/Gate 的共同形式验证预处理，无输入约束、无输出删除；它把 case4 的 case 表临时推断的组合 ROM 展开，以便 Yosys 内置 SAT 策略处理。

## PMUX 只读观察

`results/stage_counts.csv` 记录至少四个必需阶段，并额外记录 `map_ffram` 中 common `techmap.v` 前后：

- `proc`
- `basic_opt`
- `before_common_techmap`
- `after_common_techmap`
- `pre_abc9`（`synth_intel -run begin:map_luts`）
- `post_abc9`

原版 `synth_intel` 在 `map_ffram` 依次执行 `opt -fast -mux_undef -undriven -fine -full`、`memory_map`、`opt -undriven -fine`，然后执行 `techmap -map +/techmap.v`。该通用 techmap 覆盖 `$pmux` 并将其展开为基本逻辑/`$_MUX_`；因此未来研究 `pmux_opt` 的最接近候选位置是上述 `techmap -map +/techmap.v` **之前**，即 `map_ffram` 内 `opt -undriven -fine` 之后。它不是本基线的一部分。

## 交付物

- `scripts/`：构建、冒烟、单案、总运行、验证、统计脚本。
- `case1/` 至 `case4/`：完整 Yosys 脚本/日志/命令/时间、RTLIL/JSON 网表及 EQY 配置/日志/工作目录。
- `results/baseline.csv`、`results/baseline.md`：正式 LUT/DFF/IO/总 cell、CPU、峰值内存、返回码、警告、RTLIL 和 EQY 状态。
- `results/stage_counts.csv`：PMUX/MUX/比较器和主要组合 cell 的阶段统计。
