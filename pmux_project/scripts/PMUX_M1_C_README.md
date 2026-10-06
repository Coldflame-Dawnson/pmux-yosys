# C 验证入口

## 官方五阶段差异诊断

在 `integration/c-real-m1` 从仓库根目录用一条命令复现四案的五个截点、case3 七位扇出与删除链、JSON/人工汇总和阶段证据归档：

```bash
python3 pmux_project/scripts/diagnose_official_stages.py --bin /home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/bin --plugin pmux_project/pass/pmux_opt.so --source-root /home/yuewen/work/pmux-yosys/pmux_project/pmux_case/competition_case --out pmux_project/reports/c-official-stage-diff-final
```

若插件尚未编译，脚本使用指定冻结工具链的 `yosys-config` 构建它。脚本核对四个官方输入 SHA-256。五个截点各在独立 Yosys 进程运行并停在该点；最终 ABC9 截点直接调用 B 的 `run_synth_intel_pmux.py`，避免中间网表导出改变后续 ABC9 映射。baseline 不加载插件，optimized 的插入点前截点尚未运行 Pass，其余截点每次只运行一次真实 Pass。完整的逐阶段 RTLIL、JSON、SHA-256、cell 统计及差值在 `reports/c-official-stage-diff-final/`。该诊断不改变 LUT 收益的既有判定，不宣布 M1 完成。

## 真实阶段验收（2026-10-06）

以下是当前入口。下文旧的 identity/桩用法仅为 M1 框架历史记录，不能作为真实算法或 LUT 收益证据。

```bash
cd /home/yuewen/work/pmux-yosys-c-real
BIN=/home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/bin
SRC=/home/yuewen/work/pmux-yosys/pmux_project/pmux_case/competition_case
make -C pmux_project/pass YOSYS_CONFIG="$BIN/yosys-config"
TEMPLATE="python3 {project}/scripts/real_pass_adapter.py --before {before} --after {after} --plan {plan} --stats {stats} --plugin {project}/pass/pmux_opt.so --yosys $BIN/yosys"
python3 pmux_project/scripts/run_random.py --count 1000 --mode external --external-kind real --bin "$BIN" --out pmux_project/reports/c-real-random-1000 --dut-template "$TEMPLATE"
python3 pmux_project/scripts/run_official.py --mode identity --bin "$BIN" --source-root "$SRC" --out pmux_project/reports/c-official-baseline-check
python3 pmux_project/scripts/run_official.py --mode real --bin "$BIN" --plugin pmux_project/pass/pmux_opt.so --source-root "$SRC" --out pmux_project/reports/c-official-b-audited
python3 pmux_project/scripts/summarize_c_stage.py
```

真实官方模式调用 B 的 `pmux_project/pass/run_synth_intel_pmux.py`，显式传入插件，分别运行 `--stage post_abc9` 和 `--stage full`。各 case/阶段有独立 plan/stats/log/script。B 的入口及 C 的运行器均核验 PMUX_OPT 恰好执行一次，顺序为 MEMORY_MAP < PMUX_OPT < OPT_CLEAN < TECHMAP < ABC9。单独的诊断运行保存 before/after RTLIL、原生 plan/stats 并核对结构；它不参与 CPU/内存计时。官方四例的输入通过 `--source-root` 只读引用 M0 文件并校验 SHA-256。

当前 B 审计入口的四例 EQY 均通过，但四例 LUT 均没有减少，`lut_benefit=FAIL`；不得合入 `integration/g3` 或宣布 M1 完成。identity 模式仍使用未加载插件的原版单行 `synth_intel`。

从团队仓库根目录执行；以下工具路径是 M0 冻结的未修改 Yosys 0.69 安装目录。运行输出写到 `pmux_project/reports/`，不写官方 RTL、原基线结果或生产代码。

```bash
python3 pmux_project/scripts/generate_micro.py
python3 pmux_project/scripts/run_micro.py --mode identity --bin /home/yuewen/tools/yosys-0.69/bin
python3 pmux_project/scripts/run_micro.py --mode fixture-stub --bin /home/yuewen/tools/yosys-0.69/bin
python3 pmux_project/scripts/run_random.py --count 100 --bin /home/yuewen/tools/yosys-0.69/bin
python3 pmux_project/scripts/run_random.py --count 1000 --bin /home/yuewen/tools/yosys-0.69/bin
python3 pmux_project/scripts/run_random.py --seed 42 --bin /home/yuewen/tools/yosys-0.69/bin
python3 pmux_project/scripts/run_official.py --mode identity --bin /home/yuewen/tools/yosys-0.69/bin
```

`identity` 复制未改写网表；`fixture-stub` 精确重放期望位，只测试验证框架。两者的证明引擎结论均为 `SKIP/NOT_RUN`。结构、Yosys `check` 和 EQY 各自独立显示 PASS/FAIL。桩结果不能作为 A 的证明或真实 Pass 收益。四例官方身份模式只确认同口径可运行，LUT 收益记 `NOT_RUN`。

外部被测程序用 `--mode external --dut-template '命令 ...'` 接入；模板有 `{before}`、`{after}`、`{plan}`、`{stats}`、`{case}` 五个占位符。被测程序须读取输入 RTLIL，输出改写后 RTLIL 与两个 JSON 文件。计划的最小结构为 `{"schema":1,"edits":[{"module":"top","cell":"target","port":"B","bit":0,"value":"1","branch":null}]}`；统计至少含 `changed_bits` 和 `reason_codes`。实际 B 日志 schema 仍待冻结；适配器只应转换 B 的真实日志，不得根据期望清单合成真实 Pass 结果。B 桩使用 `--external-kind stub`，真实 A/B 集成后指定 `--external-kind real`。期望清单不改。

```bash
python3 pmux_project/scripts/run_micro.py --mode external --external-kind stub \
  --dut-template 'python3 {project}/scripts/fixture_stub_adapter.py --before {before} --after {after} --plan {plan} --stats {stats} --case {case}' \
  --bin /home/yuewen/tools/yosys-0.69/bin
```

上例 adapter 是 C 的 fixture 重放联通测试，不是 B 的分析器桩。接入 B 时将模板换为 B 的适配命令。真实 Pass 纳入 `synth_intel` 后，用开发工具目录运行官方回归：

B 分支 `90bc8c902` 的 UNKNOWN 桩已用 `b_unknown_adapter.py` 跑完 19 例。它仅在 B 日志明确报告 `writes=0 commit=ok` 时输出空计划；`--external-kind untriggered` 让预期正例的语义结果保持 SKIP。B 的强制结论桩没有逐位机器计划，不能对其结果伪造计划或宣称 EQY 通过。插件副本从 B 提交只读提取，在 `reports/c-m1-b-build/` 编译，未改 B 源码或基线工具链。

```bash
git fetch origin feature/b-pass-rewrite
mkdir -p pmux_project/reports/c-m1-b-build
git archive 90bc8c902 pmux_project/pass pmux_project/include | tar -x -C pmux_project/reports/c-m1-b-build
make -C pmux_project/reports/c-m1-b-build/pmux_project/pass \
  YOSYS_CONFIG="$PWD/pmux_project/official_baseline/toolchain/install/bin/yosys-config"
python3 pmux_project/scripts/run_micro.py --mode external --external-kind untriggered \
  --dut-template 'python3 {project}/scripts/b_unknown_adapter.py --before {before} --after {after} --plan {plan} --stats {stats} --plugin {project}/reports/c-m1-b-build/pmux_project/pass/pmux_opt.so --yosys {project}/official_baseline/toolchain/install/bin/yosys'
```

```bash
python3 pmux_project/scripts/run_official.py --mode real --bin /path/to/development/install/bin
```

官方比较从 M0 的 `baseline.csv` 读取资源数，核对原始输入 SHA-256，分别运行原口径完整 `synth_intel -family cycloneiv` 和 `-run begin:map_cells` 取 ABC9 后快照。EQY 先默认分区，失败时保留日志并尝试整设计路径；test3 路径保留。只有四例全部无 LUT 增加、DFF/IO 不增加且至少一例 LUT 减少才判 `lut_benefit=PASS`。每例完整日志、命令、网表、EQY 工作目录和失败原样存于 `reports/`。随机失败目录含 seed、生成前设计、版本、命令、完整日志和最小化前样本。
