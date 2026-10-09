from pathlib import Path
import json,re,csv,difflib,shutil
E=Path('/home/yuewen/work/pmux-yosys-c-621cb4e/pmux_project/experiments/case34_v02b/c_621cb4e_evidence');R=E.parents[3];O=R/'pmux_project/reports/c-621cb4e-default-final'
# Ensure fresh reproduction builds the isolated runtime without requiring an untracked preparation script.
f=E/'build_c.py';s=f.read_text();s=s.replace("shutil.copytree(source,O/'official')","shutil.copytree(source,O/'official')\nshutil.copytree(T/'bin',O/'runtime/bin',symlinks=True)\nshutil.copytree(O/'data',O/'runtime/share/yosys',symlinks=True)")
s=s.replace('O.mkdir(parents=True,exist_ok=True)','O.mkdir(parents=True,exist_ok=False)').replace("(O/'build').mkdir(exist_ok=True)","(O/'build').mkdir()")
f.write_text(s)
f=E/'snapshot_counterexample.py';s=f.read_text().replace("result['eqy']='PASS' if erc==0 and (d/'eqy_work/PASS').exists() else 'FAIL'","result['eqy']='PASS' if erc==0 and (d/'eqy_work/PASS').exists() else 'ERROR_OR_NOT_PROVED'");f.write_text(s)
a=(O/'snapshot_counterexample/post_producer.il').read_text().splitlines();b=(O/'snapshot_counterexample/changed_snapshot.il').read_text().splitlines()
(E/'counterexample_one_bit.diff').write_text('\n'.join(difflib.unified_diff(a,b,fromfile='post_producer.il',tofile='changed_snapshot.il',n=8,lineterm=''))+'\n')
results=json.loads((E/'native_results.json').read_text())
for row in results:
 for variant in ('baseline','default'):
  log=(O/'native'/row['case']/variant/'yosys.log').read_text();totals=re.findall(r'ABC: elapse: ([\d.]+) seconds, total: ([\d.]+) seconds',log)
  row[variant]['abc_elapsed_seconds_reported']=[float(x[0]) for x in totals]
  row[variant]['abc_total_seconds_reported']=[float(x[1]) for x in totals]
(E/'native_results.json').write_text(json.dumps(results,indent=2)+'\n')
lines=['# C 默认生产路径最终验收：621cb4e','', '**最终结论：BLOCKED。G3 保持 `BLOCKED_BY_UNIFIED_SELECTION_GATE`。**','',
'唯一已确认的阻塞条件：真实 constant-table 改写发布合法授权后、默认 `synth_intel` 消费授权前，目标 `pmux_case34_lut4` 的 LUT 参数改变一个二进制位，默认选择仍为 `accepted_constant_table`，没有按本轮要求回退 `stock`。','',
'本轮未修复生产代码，也未合并 integration/g3 或主分支。按用户要求“若发现生产缺陷，立即停止生产候选验收，保存最小反例并报告 BLOCKED”，发现该反例后终止回归并归档。未执行或未完成项目均标为 UNVERIFIED。','',
'## 固定提交、授权与环境','',
'- C 分支：`feature/c-v02-default-path-final-acceptance-621cb4e`。','- C 起点 / 被验收 B：`621cb4e56b734da73cfb6161b951a375893e2d81`。','- B 分支：`feature/b-v02-unified-selection-gate`。','- B 直接父提交：`9107a466f74ce27269361ee26e99093c1b8fc93e`；merge-base --is-ancestor 返回 0。','- A 审计内容：`3cc303df7ea1336659f137b564453aab76c76b66`；A HEAD：`19b8dee568082393aa2f5a77091fef860dd4fe68`；报告结论 AUTHORIZED，仅作为只读授权，未合入 C。','- 远端：`https://github.com/Coldflame-Dawnson/pmux-yosys.git`。','- Yosys/EQY/SBY：0.69；Yosys 源码 `9f75ca1f9834a39a863915b5dae0c7b1e33533bc`；EQY `4a72eb94fc253062464afee4d0018359017bb846`；SBY `b1a1e98cba941ec8433f8dc27f416cd7bb7f14be`。','- 四例 gold 与用户原始 `比赛要求/pmux_case/competition_case/test[1-4].v` 的 SHA-256 逐一相同，见 `official_input_comparison.json`。','',
'## 默认路径如何独立运行','',
'C 使用固定 Yosys 0.69 二进制和从固定 B 提交重新编译的真实 PMUX 插件；另把 B 的 `techlibs/intel/synth_intel.cc` 原文作为插件编译。`native_synth_loader.cc` 仅移除预装 stock synth_intel 的注册项，再注册未经修改的 B SynthIntelPass；没有替换其选择、证明或映射实现。四例直接调用相同的 `synth_intel -family cycloneiv -top <top> -run begin:map_cells`，没有调用实验 runner。','',
'数据目录使用固定安装文件的独立副本，加上 B 提交中的两份 intel/common 映射资产；Yosys 可执行文件按字节复制。首次设置 YOSYS_DATDIR 未覆盖该二进制的实际数据目录，case4 缺少映射文件；该环境探测日志保留于 `setup_probe_native_original_datadir/`。完成正确的独立运行目录配置后重新生成全部四例 gate，未复用探测 gate。','',
'EQY 配置直接使用 B 的 eqy_config 函数，包含 whole-design merge *、SAT depth 20 和 ABC PDR，不改判据。资源统计保持 `$lut` 和 `dffeas` 口径；IO 为顶层端口位数；post-ABC9 cell 数为顶层 cell 数。case2 不混用 cycloneiv_lcell_comb 或 LUT6 口径。最终 Intel 映射 RTLIL 另外保存在每例 `final.il`。','',
'## 四例独立结果','',
'| case | baseline LUT/DFF | default LUT/DFF | IO / 增量 | 总 cell | structural edit | profile | whole-design EQY |','|---|---:|---:|---:|---:|---:|---|---|']
for x in results:
 b,v=x['baseline'],x['default'];lines.append(f'| {x["case"]} | {b["lut"]}/{b["dff"]} | {v["lut"]}/{v["dff"]} | {v["io"]}/+{v["io"]-b["io"]} | {v["cells"]} | {v["structural_edits"]} | {v["profile"]} | {x["eqy"]} |')
lines+=['','以上仅为未注入故障的官方四例结果，不能替代负例门禁验收。每例 post_abc9.json、post_abc9.il、final.il、完整综合日志、EQY 配置、EQY 工作目录及 PASS 标记均由 C 本轮生成。','',
'| case | CPU user+system / s | ABC 自报 total / s | 峰值 RSS / KiB |','|---|---:|---:|---:|']
for x in results:
 v=x['default'];lines.append(f'| {x["case"]} | {v["user_seconds"]+v["system_seconds"]:.2f} | {v["abc_total_seconds_reported"]} | {v["peak_kib"]} |')
lines+=['','CPU/RSS 由 GNU time -v 记录；ABC total 为日志自报值（低精度），完整行保存在 native_results.json。输入和 gate 的完整 SHA-256 在 results.csv 与 SHA256SUMS。','',
'## 最小变更反例','',
'测试夹具 `producer_observer.cc` 保存原始 PMUX Pass 指针并调用原始 execute；真实 proof/atomic commit 完成、scratchpad 为 true 后，仅翻转第一个实际遍历到的 marker cell 的 LUT[0]。该夹具不伪造证明、授权或 profile，也不按 cell 名分派。故障发生在映射消费前，属于本轮明确要求覆盖的快照变化负例。它是受控故障注入，不表示未经干扰的官方 case4 综合失败。','',
'原参数与变更：','',
'```text','cell type: pmux_case34_lut4','LUT before: 16\'1111111101111110','LUT after:  16\'1111111101111111','authorization at mutation: true','expected profile after snapshot change: stock','observed native profile: accepted_constant_table','scratchpad after invocation: false (assertion PASS)','```','',
'`counterexample_one_bit.diff` 显示两个阶段 RTLIL 唯一差异是该参数位。默认流程实际执行 cofactor techmap 和专用 ABC 脚本（完整日志及 assertions 摘录已保存）。`synth_intel.cc:266-274` 的消费检查只有 scratchpad 布尔值与 marker cell 类型；未绑定提交时的 marker 参数/连接快照，因此本次变更未被拒绝。runner 也采用布尔值和类型联合检查；本轮阻塞以直接执行的 native 证据为准。','',
'反例继续使用冻结 EQY 配置，其总返回码为 1，SAT UNKNOWN，PDR 因 `$lut` maybe-x 预处理不支持返回 ERROR，没有 PASS 标记。**不能把该 ERROR 写成形式化证明“不等价”；正式不等价结论 UNVERIFIED。** 此限制不影响已经直接观察到的 profile 拒绝条件违规。EQY 日志和 SAT trace.vcd 均归档。','',
'复现顺序（在相同固定工具环境、新的空报告目录）：','',
'```bash','python3 pmux_project/experiments/case34_v02b/c_621cb4e_evidence/build_c.py',
'/home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/bin/yosys-config --build pmux_project/reports/c-621cb4e-default-final/build/producer_observer.so pmux_project/experiments/case34_v02b/c_621cb4e_evidence/producer_observer.cc',
'python3 pmux_project/experiments/case34_v02b/c_621cb4e_evidence/snapshot_counterexample.py','```','',
'反例脚本预期返回非零以报告门禁失败。所有真实执行命令另见 commands.txt；脚本定位当前 C 工作树和固定工具目录。','',
'## 回归与授权消费状态','',
'| 项目 | C 独立结果 |','|---|---|','| 19 微测试 | 19/19 PASS，结构/check/EQY/semantic |','| 默认模式固定 1000 种子 | 582 个已完成且 PASS；余下停止，完整 1000 UNVERIFIED |','| 显式 -v02-case34 固定 1000 种子 | 0 个完成；停止，UNVERIFIED |','| production mutation seam 26 | NOT RUN / UNVERIFIED |','| A 17 项统一行为覆盖 | NOT RUN / UNVERIFIED |','| Analyzer fast / slow | 已独立编译；测试 NOT RUN / UNVERIFIED |','| 默认只读前后指纹与重复确定性 | NOT RUN / UNVERIFIED |','| case4 32×10 官方逐行核对 | NOT RUN / UNVERIFIED |','| native 首次合法消费 / scratchpad 清零 | accepted_constant_table / 清零断言 PASS |','| 重建 cell 后第二次 stock / 完整重放 | NOT RUN / UNVERIFIED |','| 目标 cell 快照变化拒绝 | FAIL：仍 accepted_constant_table |','| 其他未授权、字段不全、非法 profile、类型不符、歧义负例 | NOT RUN / UNVERIFIED |','',
'未完成项目是发现唯一生产阻塞后按用户指令停止的结果；不能引用 A/B 的 PASS 替代。A 原 13/12 项审计及 fast/slow 计数仅保留在只读授权报告中，不计为 C 成绩。','',
'## 逐项判定','',
'| 条目 | 判定 | 理由 |','|---|---|---|','| P0 | P0_PASS（仅官方四例） | 全部 whole-design EQY PASS |','| P1 | UNVERIFIED | 四例使用相同入口；完整身份分派审计停止，不能给 P1_PASS |','| P2 | P2_PASS（仅官方四例） | 冻结 LUT/DFF、IO 增量、profile 全符合 |','| DEFAULT_PATH | BLOCKED | 快照变更后未回退 stock |','| ONE_SHOT_AUTHORIZATION | UNVERIFIED | 首次清零已验证，完整 cell 重生重放尚未执行 |','| PRODUCTION_CANDIDATES | BLOCKED | 不满足默认生产路径负例要求 |','| G3 | BLOCKED_BY_UNIFIED_SELECTION_GATE | 不可更新为 PASS |','',
'## 归档、提交范围与远端核验','',
'Git 中仅增加本报告及 `c_621cb4e_evidence/` 下的 C 验收脚本、夹具、报告和小型清单；无生产源码修改。大型运行数据保存于 `pmux_project/reports/c-621cb4e-default-final/`（Git 忽略）。MANIFEST.md 逐项说明用途、命令、输入、SHA-256 和 Git 状态；完整机器文件清单位于忽略目录的 artifact_inventory.jsonl，由 Git 跟踪的摘要绑定。未找到的预期结果列为 MISSING，未独立完成的判定列为 UNVERIFIED。','',
'提交后的完整 C HEAD、tracking ref、GitHub 远端 branch HEAD 和工作树状态保存在交付副本 `delivery_verification.json`，避免把当前提交自身哈希写进同一提交造成自引用。推送仅限指定 C 分支。']
(E.parent/'C_DEFAULT_PATH_FINAL_ACCEPTANCE_621cb4e.md').write_text('\n'.join(lines)+'\n')
print('REPORT_WRITTEN')