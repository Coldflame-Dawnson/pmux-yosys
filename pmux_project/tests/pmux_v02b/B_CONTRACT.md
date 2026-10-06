# B v0.2-B 机器接口联调（test-only）

联调分支从 C 提交 `2f741bb2bee8b339d4c0b8dbbbfa318ac12fdc0c` 建立，并 cherry-pick B 提交 `bff595118a130756f504af99c09000c98adda4ba`。没有 cherry-pick A 的生产证明实现；B 的 `-v02-branch-delete` 在无证书提供者时是 no-op。

`run_v02b_b_contract.py` 直接加载 B 的 `pmux_opt.so`，读取 Pass 写出的 `plan.json`、`stats.json` 与 after RTLIL。它核对 `branch_edits` 的 deleted_branches、B 物理范围、S 索引、宽度、目标类型和 transformation，核对 `changed_bits == len(edits)`、提交与回退状态，然后执行结构断言、Yosys check、EQY、ABC9 前后综合及 DFF/IO 回退检查。

定向 22 例中，安全删除例注入 `-test-branch-proof`；部分证明、keep、快照失配、非法索引用 B 的 test-only 故障注入选项。其余语义拒绝与预算例未注入证书，只验证“无提供者时不改写”；它们的 A 证明原因码状态是 **NOT_VERIFIED**。固定种子 100 例中，50 个互斥选择例注入证书，50 个可能重叠例无证书。所有结果均标记为 `B_TEST_CERTIFICATE_CONTRACT`，不能计入真实 Pass 100/1000 验收。

case3 使用 `c-official-stage-diff-final/stage-evidence.tar.gz` 的 `case3/baseline/01_pre_pmux.il`，SHA-256 固定为 `baa2a34eb6fd25db3950defbc141a1477d7114b38eee971d9b02cca12d84af1e`。C 直接运行 B 插件的 test certificate，核对目标模块 JSON 与冻结 B V2-B 一致，随后沿官方阶段口径执行 check、EQY 和 ABC9；13→12 LUT 仅表示 test-only 结构路径仍可复现。

## 运行

在团队仓库根目录执行，`BIN` 指向 M0 冻结 Yosys/EQY 目录；每次重跑使用新的输出目录：

```sh
BIN=/home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/bin
make -C pmux_project/pass YOSYS_CONFIG="$BIN/yosys-config"
python3 pmux_project/scripts/run_v02b_b_contract.py --suite micro --bin "$BIN" --plugin pmux_project/pass/pmux_opt.so --out pmux_project/reports/c-v02b-b-contract-22-rerun
python3 pmux_project/scripts/run_v02b_b_contract.py --suite random --count 100 --bin "$BIN" --plugin pmux_project/pass/pmux_opt.so --out pmux_project/reports/c-v02b-b-contract-random100-rerun
mkdir -p pmux_project/reports/c-v02b-b-contract-input
tar -xOf pmux_project/reports/c-official-stage-diff-final/stage-evidence.tar.gz case3/baseline/01_pre_pmux.il > pmux_project/reports/c-v02b-b-contract-input/case3.frozen.before.il
python3 pmux_project/scripts/run_v02b_b_case3_mock.py --bin "$BIN" --plugin pmux_project/pass/pmux_opt.so --before pmux_project/reports/c-v02b-b-contract-input/case3.frozen.before.il --expected-json pmux_project/tests/case3_variants/b_handoff/b.json --official-source /home/yuewen/work/pmux-yosys/pmux_project/pmux_case/competition_case/test3.v --out pmux_project/reports/c-v02b-b-case3-mock-rerun
```

证据归档分别位于各输出目录的 `evidence.tar.gz`，含 before/after、plan、stats、完整日志、综合阶段网表、EQY 和 SHA-256。`summarize_v02b_gate.py` 的 `--b-contract-micro`、`--b-contract-random100`、`--b-case3-mock`、`--b-test-commit` 只填 test-only 栏；真实验收栏保持 NOT_RUN，`ready_to_merge_integration_g3=false`。
