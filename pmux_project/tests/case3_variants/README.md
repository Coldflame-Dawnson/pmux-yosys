# case3 V2 实验矩阵输入契约

此目录只存实验输入清单及 B 的五个冻结 RTLIL 输出，不修改官方 test3.v 或默认 Pass。五个输入来自 B 的 experiment/b-case3-v02-ablation 分支提交 fbb9eeb40723ad076e4959d53707827082aa40bd。附件 summary.json 与该提交逐字节一致，B 的原始输入哈希也与 C 已归档的官方 case3 插入点快照一致。

## B 交付格式

每个变体提供一个 RTLIL 截点，位置是 opt -undriven -fine 之后、opt_clean 之前。B 交付的 after.il 含 \test3 及 FPGA cell 库模块。运行器先核对完整 RTLIL 与 JSON 的 SHA-256，再用 Yosys 取出顶层 \test3；每个阶段重建官方 FPGA cell 库上下文，替换顶层后执行 check -assert -noinit。规范化后的顶层 JSON 必须与 B 的原始 after.json 顶层结构完全相同。顶层规范化不改变 B 的原始 after.il；两份文件均保存于证据归档。

manifest.json 中每个变体记录 B 交付路径和完整文件 SHA-256，例如：

    {
      "rtlil": "b_handoff/a.il",
      "sha256": "8c9e308670bc23f8198ef76cb2e6ccf6bb51312f1acc7a235408370c1549b1e5",
      "json": "b_handoff/a.json",
      "json_sha256": "81c07d252dc32ae5a5df1a2ecda21163dc673c4385c73572d802fc6dd1b9bcca"
    }

路径相对于 manifest 所在目录；也可用绝对路径。五个键必须齐全。运行器还核对 B summary.json 的哈希、原始快照哈希及各变体 after.il/after.json 哈希。实验中的六条流程共享同一个官方 case3 RTL 和冻结 Yosys/EQY；baseline 截点由运行器从原始 RTL 生成，并要求最终 ABC9 JSON 与 C 已归档的官方 baseline 完全相同。各阶段使用独立 Yosys 进程，导出不会影响后续 ABC9 映射。

## 命令

从仓库根目录运行（将工具链和原始 RTL 路径设为 M0 冻结路径）：

    python3 pmux_project/scripts/run_case3_variant_matrix.py --bin /home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/bin --source /home/yuewen/work/pmux-yosys/pmux_project/pmux_case/competition_case/test3.v --baseline-only --out pmux_project/reports/c-case3-v2-baseline-smoke

五个变体已经填入 manifest，下面第二条命令运行真实矩阵。--smoke-with-baseline 会把五个候选都替换成 baseline 截点，只用于验证矩阵入口；其结果标记 HARNESS_ONLY，不属于真实变体验收。

    python3 pmux_project/scripts/run_case3_variant_matrix.py --bin /home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/bin --source /home/yuewen/work/pmux-yosys/pmux_project/pmux_case/competition_case/test3.v --manifest pmux_project/tests/case3_variants/manifest.json --out pmux_project/reports/c-case3-v2-real-matrix

验收仅以 ABC9 后 $lut 严格减少为收益。EQY 失败、DFF/IO 增加直接拒绝；LUT 未下降为 NO_GAIN。若多个变体有收益，报告按规则集合包含关系给出最小变体；可能有多个互不包含的最小变体。所有变体为 13 LUT 时，结论为“当前三类规则不值得进入生产实现”。框架不修改生产代码、官方 RTL，也不合入 integration/g3。
