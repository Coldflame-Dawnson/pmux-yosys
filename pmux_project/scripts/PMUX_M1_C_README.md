# C 验证入口

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

```bash
python3 pmux_project/scripts/run_official.py --mode real --bin /path/to/development/install/bin
```

官方比较从 M0 的 `baseline.csv` 读取资源数，核对原始输入 SHA-256，分别运行原口径完整 `synth_intel -family cycloneiv` 和 `-run begin:map_cells` 取 ABC9 后快照。EQY 先默认分区，失败时保留日志并尝试整设计路径；test3 路径保留。只有四例全部无 LUT 增加、DFF/IO 不增加且至少一例 LUT 减少才判 `lut_benefit=PASS`。每例完整日志、命令、网表、EQY 工作目录和失败原样存于 `reports/`。随机失败目录含 seed、生成前设计、版本、命令、完整日志和最小化前样本。
