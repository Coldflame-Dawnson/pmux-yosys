# case3 V2 实验矩阵输入契约

此目录只存实验输入清单，不修改官方 test3.v 或默认 Pass。当前五个输入为 null，表示 B 尚未交付，结论保持 NOT_RUN。

## B 交付格式

每个变体提供一个仅包含 \test3 模块的 RTLIL 截点，位置是 opt -undriven -fine 之后、opt_clean 之前。可用 select test3; write_rtlil -selected <file.il> 导出。B 可以在此截点前执行实验变换，但不得把变换接入默认 Pass。运行器会先重建官方 FPGA cell 库上下文，再读入变体并执行 check -assert -noinit。

在 manifest.json 中将对应 null 改为对象：

    {
      "rtlil": "inputs/V2-A.il",
      "sha256": "<该文件的完整 SHA-256>"
    }

路径相对于 manifest 所在目录；也可用绝对路径。五个键必须齐全。运行器先核对输入 SHA-256，再将输入复制到证据目录。实验中的六条流程共享同一个官方 case3 RTL 和冻结 Yosys/EQY；baseline 截点由运行器从原始 RTL 生成。为保留 FPGA cell 库，运行器在每个阶段先运行相同官方前缀，再替换顶层 \test3 为变体 RTLIL，然后继续 B 审计入口的共同后缀。各阶段使用独立 Yosys 进程，导出不会影响后续 ABC9 映射。

## 命令

从仓库根目录运行（将工具链和原始 RTL 路径设为 M0 冻结路径）：

    python3 pmux_project/scripts/run_case3_variant_matrix.py --bin /home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/bin --source /home/yuewen/work/pmux-yosys/pmux_project/pmux_case/competition_case/test3.v --baseline-only --out pmux_project/reports/c-case3-v2-baseline-smoke

收到五个变体后，填好 manifest 并去掉 --baseline-only，将 --out 指向新的结果目录。--smoke-with-baseline 会把五个候选都替换成 baseline 截点，只用于验证矩阵入口；其结果标记 HARNESS_ONLY，不属于真实变体验收。

    python3 pmux_project/scripts/run_case3_variant_matrix.py --bin /home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/bin --source /home/yuewen/work/pmux-yosys/pmux_project/pmux_case/competition_case/test3.v --manifest pmux_project/tests/case3_variants/manifest.json --out pmux_project/reports/c-case3-v2-real-matrix

验收仅以 ABC9 后 $lut 严格减少为收益。EQY 失败、DFF/IO 增加直接拒绝；LUT 未下降为 NO_GAIN。若多个变体有收益，报告按规则集合包含关系给出最小变体；可能有多个互不包含的最小变体。所有变体为 13 LUT 时，结论为“当前三类规则不值得进入生产实现”。框架不修改生产代码、官方 RTL，也不合入 integration/g3。
