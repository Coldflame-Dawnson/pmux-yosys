# A v0.2-B 证明接口独立核对

使用独立 detached 检出 `3aff789a3badf6fecc4d3651e4b7eea52627fe3c` 和冻结 Yosys 0.69，未合入 C 分支、未修改生产代码。

| 核对 | 结果 |
|---|---|
| 普通自测 | PASS，2116 assertions |
| 慢速预算自测 | PASS，2118 assertions |
| 新增分支自测 | PASS，29 assertions |
| case3 全量证明扫描 | `$procmux$15` branch 1、fallback=A、`branch_redundant`、3 cells/11 bits/45 events |
| case3 RTLIL 只读指纹 | `BRANCH_PROOF_NETLIST_UNCHANGED 1` |
| B 生产 Pass 改写、EQY、ABC9 | NOT_RUN |

原生命令：

    make -C pmux_project/analyzer YOSYS_ROOT=/home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install test
    make -C pmux_project/analyzer YOSYS_ROOT=/home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install test-slow
    YOSYS_DATDIR=/home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/share/yosys /home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/bin/yosys -Q -m ./analyzer_selftest.so -p 'read_rtlil /home/yuewen/work/pmux-yosys/pmux_project/official_baseline/case3/netlists/stage_proc.il; pmux_branch_proof_report'

第三条从隔离检出的 `pmux_project/analyzer` 目录运行。A 的文档未定义独立的深度预算原因码；需 B 给出机器契约后再核对第 15a 类。fixture 原因类别与 A/B 真实原因码始终分开统计。
