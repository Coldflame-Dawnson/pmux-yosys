#!/usr/bin/env python3
"""C independent result audit, identity census and content-addressed evidence archive."""
import hashlib,json,re,subprocess,sys
from pathlib import Path
E=Path(__file__).resolve().parent; R=E.parents[3]; O=R/'pmux_project/reports/c-b8368b0-default-final'; B='b8368b0f3e2d55a2bb429296a3aea859eb4f2049'; A='d03381f4511ab520372f3c2e57657b141a4959bc'
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
 return h.hexdigest()
def doc(p):return json.loads(p.read_text())
def git(*args):return subprocess.check_output(['git',*args],cwd=R,text=True).strip()
# Independent aggregation, requiring every item rather than trusting the aggregate totals.
micro=doc(O/'micro/summary.json');assert len(micro['rows'])==19
for row in micro['rows']:assert all(row[k]=='PASS' for k in ['structure','yosys_check','eqy','semantic','result']),row
random=doc(O/'random_summary.json'); seeds=set(map(int,(R/'pmux_project/tests/pmux_m1/seeds_1000.txt').read_text().split()))
assert len(seeds)==1000 and len(random['rows'])==2000
for mode in ['default','explicit']:
 rows=[r for r in random['rows'] if r['mode']==mode];assert len(rows)==1000 and {r['seed'] for r in rows}==seeds
 for row in rows:
  assert row['pass'] and row['rc']==0 and row['row']['eqy']=='PASS' and row['row']['proof_engine']=='REAL_RUN',row
  assert doc(Path(row['summary']))['rows'][0]==row['row']
  assert (Path(row['summary']).parent/f"seed_{row['seed']:08d}"/'eqy_work/PASS').is_file()
lifecycle=doc(E/'lifecycle_matrix.json');assert lifecycle['passed']==lifecycle['total']==len(lifecycle['rows'])==58
for row in lifecycle['rows']:assert row['pass'] and row['rc']==0 and row['pre_effect_four_field_assertions']
for mode in {r['mode'] for r in lifecycle['rows']}:
 rows=[r for r in lifecycle['rows'] if r['mode']==mode];assert len(rows)==2
 assert rows[0]['profiles']==rows[1]['profiles'] and rows[0]['reasons']==rows[1]['reasons']
native=doc(E/'native_results.json'); oracle=[(288,222,288,222,'stock'),(25,0,25,0,'stock'),(13,17,11,12,'stock'),(36,10,27,10,'accepted_constant_table')]
for i,(row,expected) in enumerate(zip(native,oracle),1):
 b,d=row['baseline'],row['default'];assert (b['lut'],b['dff'],d['lut'],d['dff'],d['profile'])==expected
 assert b['io']==d['io'] and row['eqy']=='PASS' and row['checks_pass']
 w=O/f'native/case{i}/eqy/work';assert (w/'PASS').is_file()
 assert sha(O/f'official/test{i}.v')==row['gold_sha256']
 assert sha(O/f'native/case{i}/default/post_abc9.il')==d['gate_sha256']
core=doc(E/'core_results.json');assert core['production_mutations']=={'passed':26,'checks':26}
assert core['analyzer_fast']['pass'] and core['analyzer_slow']['pass']
assert all(core[k]['fingerprint_unchanged'] for k in ['case3_decision','case3_production','case4_production'])
assert core['table32']=={'rows':32,'width':10,'address0':'1001110000','all_rows_match':True}
# Static identity scan includes every production translation unit/header, excluding test/report plugins.
sources=list((R/'pmux_project/pass').glob('*.cc'))+list((R/'pmux_project/pass').glob('*.h'))+[R/'techlibs/intel/synth_intel.cc',R/'pmux_project/include/pmux_opt_api.h']
sources += [p for p in (R/'pmux_project/analyzer').glob('*') if p.suffix in ['.cc','.h'] and not any(x in p.name for x in ['selftest','report_plugin'])]
scan=[];forbidden=re.compile(r'\btest[1-4]\b|\b(?:datain|dataout|gpio)\b|1001110000|\$procmux\$')
identity=re.compile(r'\.name\b|->name\b|module_id|cell_id|\.str\(\)|filename|cofactor_key|generation')
for p in sorted(set(sources)):
 text=p.read_text();hits=[]
 for n,line in enumerate(text.splitlines(),1):
  assert not forbidden.search(line),(str(p),n,line)
  if identity.search(line):hits.append({'line':n,'text':line.strip()})
 scan.append({'path':str(p.relative_to(R)),'sha256':sha(p),'identity_related_lines':hits})
(E/'p1_identity_census.json').write_text(json.dumps({'forbidden_pattern':forbidden.pattern,'forbidden_hits':0,'sources':scan,'verdict':'PASS'},indent=2)+'\n')
(E/'p1_identity_review.md').write_text('''# C P1 身份无关性扫描

结论：PASS。`p1_identity_census.json` 保存生产翻译单元、头文件的 SHA-256 和逐行身份相关引用。没有官方 case 名、固定官方端口名、固定地址零向量或 `$procmux$` 自动 cell 名匹配。静态扫描需结合以下人工分类，不能仅凭关键词零命中证明任意程序属性。

- `pmux_opt.cc` 构造 request 时记录实际 module/cell ID；筛选来源为操作类型、位宽、连接和证明结果。ID 是对象索引和 trace 元数据。
- `case34_rewrite.cc` 使用 request/proof 的实际 ID 找回同一设计对象、验证 proof 与 rewrite 一致，处理 claimed cell 冲突；不是按官方身份启用规则。1280–1410 行遍历 selected modules 并调用两个结构证明 analyzer，生成、验证和应用计划。
- `rewrite_plan.cc` 的名字、指针和连接检查用于提交前定位已证明目标，防止对象变化。
- `pmux_profile_authorization.h` 的 M/C 名字、端口名、参数和属性构成完整快照；Design 指针是 monitor registry 的归属键，generation 绑定存活 monitor。它们限制已发布授权的消费，不能产生新的结构证明授权。58 个独立夹具覆盖名字/内容相同但对象已重建的拒绝。
- Analyzer 的 module_id/cell_id 相等检查是 request 合法性和 proof graph 定位，名字排序用于确定性。`cofactor_key` 根据实际 address/support/cofactor LUT 结构计算，不比较官方固定向量。
- `synth_intel.cc` 的字符串分割用于 `-run` 参数阶段解析；profile 消费调用统一授权 helper。runner 也调用同一 helper。两条消费者的 29 个逐项 profile/reason 完全一致。

P1 的范围是结构候选发现和选择不按设计身份分派。授权快照保留对象身份以拒绝重放，属于必须验证的生命周期边界，不构成按 case 身份优化。
''')
# No protected/production file may change, even accidentally.
assert git('diff',B,'--','pmux_project/pass','techlibs/intel','pmux_project/analyzer','pmux_project/include','pmux_project/tests','pmux_project/pmux_case')==''
for p in E.glob('*.csv'):p.write_bytes(p.read_bytes().replace(b'\r\n',b'\n'))
verdict={k:'PASS' for k in ['P0','P1','P2','GENERATION_MONITOR_BINDING','ONE_SHOT_AUTHORIZATION','DEFAULT_PATH','PRODUCTION_CANDIDATES','G3']}
(E/'final_verdict.json').write_text(json.dumps({'B':B,'A_authorization':A,'verdicts':verdict,'micro':19,'lifecycle':58,'default_seeds':1000,'explicit_seeds':1000,'candidate_errors':[],'unverified_required_items':[]},indent=2)+'\n')
report=R/'pmux_project/experiments/case34_v02b/C_DEFAULT_PATH_FINAL_ACCEPTANCE_b8368b0.md'
table='\n'.join(f"| {i} | {r['baseline']['lut']}/{r['baseline']['dff']} | {r['default']['lut']}/{r['default']['dff']} | 0 | {r['default']['profile']} | PASS |" for i,r in enumerate(native,1))
lifetable='\n'.join(f"| {mode} | {' → '.join(next(r for r in lifecycle['rows'] if r['path']=='native' and r['mode']==mode)['profiles'])} | {', '.join(next(r for r in lifecycle['rows'] if r['path']=='native' and r['mode']==mode)['reasons']) or '—'} | PASS / PASS |" for mode in dict.fromkeys(r['mode'] for r in lifecycle['rows']))
report.write_text(f'''# C 对 b8368b0 的最终默认路径验收

结论：**G3 = PASS**；P0/P1/P2、GENERATION_MONITOR_BINDING、ONE_SHOT_AUTHORIZATION、DEFAULT_PATH、PRODUCTION_CANDIDATES 全部 PASS。所有要求的最终回归完整完成，没有最终 ERROR、UNKNOWN、MISSING 或 UNVERIFIED 项。

## 冻结起点与分支

- C 分支：`feature/c-v02-default-path-final-acceptance-b8368b0`。
- 本报告所在的 C 完整提交用 `git rev-parse HEAD` 获取，并列于交付目录 `DELIVERY_VERIFICATION.json`（完整提交不能嵌入自身被哈希内容）。
- B：`{B}`；直接父提交：`ec86209a61f22020707b299f5175f02b8d71495d`。
- A 授权：`{A}`，分支 `feature/a-v02-generation-monitor-reaudit-b8368b0`，报告 `pmux_project/analyzer/generation_monitor_reaudit_b8368b0.md`，AUTHORIZED。A 报告仅以只读证据复制，无合并。
- 所有工具、输入、运行环境及实际可执行文件 SHA-256 在 `c_b8368b0_evidence/frozen_provenance.json`。固定 Yosys/EQY/SBY/ABC 源码提交均逐项核对。
- 独立 worktree 从精确 B 建立。全部 gate/EQY/日志从本轮不存在的输出目录生成；旧 C 驱动仅作代码模板，不复用旧结果。

## 完整回归

19/19 真实微测试 PASS；production mutation seam 26/26 PASS。Analyzer fast 的五组 assertion 数为 2116/29/60/50/81，slow 为 2118/29/60/50/81，全部 PASS。

默认模式固定种子 **1000/1000 PASS**；显式 `-v02-case34` 固定种子 **1000/1000 PASS**。两组独立目录均使用冻结的 1000 个唯一 seed，清单 SHA-256 `{random['seed_inventory_sha256']}`；每项 structure、真实 EQY 和 synthesis PASS。逐项原始结果在 `reports/c-b8368b0-default-final/random_summary.json`，逐 seed 命令、gate 和 EQY work/PASS 在 random 子目录。冻结随机检查器与验收 oracle 未修改。

case3 decision/production 和 case4 production 的只读报告前后 RTLIL SHA-256 相同，详见 core_results.json。case4 全部 32×10 位逐行与官方 RTL 核对，地址 0 为 `1001110000`；CSV 保存两侧位串。P1 生产源码扫描和人工分类见 p1_identity_review.md、p1_identity_census.json。

## 官方四例

| case | baseline $lut/DFF | 最终 $lut/DFF | IO 增量 | profile | whole-design EQY |
|---|---:|---:|---:|---|---|
{table}

四例 gold 均为本轮复制并比对哈希的官方 RTL。真实 source-loaded `synth_intel` 默认路径生成 post-ABC9 gate、JSON，随后完成 map_cells 后保存最终网表。oracle 的资源统计和 whole-design EQY 使用 post-ABC9 gate；完整后续映射网表另存 final.il。EQY 采用冻结配置、`[collect] merge *`，四个工作目录均有 PASS。baseline 仅使用同一个真实源加载入口加 `-no-pmux-v02`。无 B/A 已生成 gate 参与。

统计 JSON/CSV 同时保存 IO、总 cell、结构改写数、CPU/ABC 时间和峰值内存。case1/2 无结构改写；case3/4 各 1，分别 stock 和 accepted_constant_table。

## 生命周期与消费者一致性

**58/58 PASS**，即 native source-loaded synth_intel 与 pmux_opt_abc9 runner 各 29 个夹具。C 拦截真实生产 pass 的执行边界，调用未改动生产逻辑；只在合法发布后修改被测状态。runner 的扰动在通用 techmap/opt 完成后、授权消费者前实施，native 在 producer 后实施。每个夹具保留阶段 RTLIL、真实日志、运行环境与断言。

| 模式 | 两条消费者相同 profile | 相同拒绝原因 | native / runner |
|---|---|---|---|
{lifetable}

forged 的三种不同非空字符串、旧代和另一真实 Design 的 generation，均观察到 lifecycle_valid=1、lifecycle_monitor_match=0、profile=stock、reason=target_lifecycle_mismatch。另一 Design 的 token 由真正发布操作生成，其在所属 Design 上 monitor_match=1。两次连续发布只可消费当前代。

同名 cell 完整重建、同名 module 重建以及端口改变后恢复，payload_equal=1 且 lifecycle_valid=0，均拒绝；allocator_reuse 在两条路径实际观察到 pointer_reused=1 后仍拒绝（该项环境 GLIBC_TUNABLES=glibc.malloc.tcache_count=0）。首次消费后完整重生目标 cell，无授权或强制回放旧四字段均 stock。

所有成功及拒绝路径均通过真实 techmap/ABC9 执行前的四字段清空断言：authorization=false、snapshot=空、generation=空、lifecycle=false。最终逐项比较两条消费者所有 profile 和 reason；29/29 一致。

## 判定与边界

| 判定 | 结果 | 独立证据 |
|---|---|---|
| P0 | PASS | 19 微测试、两组 1000 种子、四例 whole-design EQY |
| P1 | PASS | 生产源码身份扫描、人工检查、结构候选与快照身份用途分类 |
| P2 | PASS | 四例严格 LUT/DFF/IO oracle |
| GENERATION_MONITOR_BINDING | PASS | 真实 forged/旧代/跨 Design/注销/重建/地址复用拒绝 |
| ONE_SHOT_AUTHORIZATION | PASS | 副作用前四字段清零、完整重生和强制旧授权回放拒绝 |
| DEFAULT_PATH | PASS | 精确 B 的 source-loaded synth_intel；29/29 runner 语义一致 |
| PRODUCTION_CANDIDATES | PASS | case3/4 结构改写、资源达标、官方 gold EQY PASS |
| G3 | PASS | 所有必需 gate 完整满足 |

## 所有调试差异、ERROR 与覆盖修正

以下是 C 测试夹具的准备阶段记录，不计入最终 58 项；原始输出单独保留，未用生产修改或 oracle 放宽消除失败。

1. C 编译时 unique_ptr 的 log_assert 参数改为显式 !=nullptr；未改生产代码。
2. 初始 allocator 实验虽然拒绝正确，但未实际复用地址，覆盖断言失败。因此未写 PASS，归档 lifecycle_setup_attempt_no_address_reuse。关闭 glibc tcache 并用普通 Design API 重建多个实际目标，最终两个消费者均必须观察到地址复用。后续布局变化再次使单目标覆盖不足，归档 lifecycle_setup_attempt_allocator_context 后扩大被重建对象范围；拒绝断言不变。
3. 尝试直接使用受保护 Cell 构造/添加 API 编译失败，已放弃。该次独立编译输出没有另存完整日志（准备阶段 MISSING），源代码调整过程由 C 脚本保留；最终已归档成功编译日志。此试验没有生成候选验收结果。
4. 初始 replay 夹具保存独立 Module clone，Yosys IdString GC 未追踪它，第一次映射写后端发生 unordered_map::at 异常（rc=-6），尚未第二次消费。归档 lifecycle_setup_attempt_unowned_clone；改用 Design 持有 clone 后通过。
5. 初始额外 type 变异在 runner 通用映射前注入，导致其到达消费者的状态与 native 不同：两条路径都 stock，但 snapshot/lifecycle 拒绝原因不同。归档 lifecycle_setup_attempt_mixed_boundary 全部原始行，调整 C 扰动时机到相同消费者边界后逐项原因一致。

6. 新增交付脚本后重建哈希清单时，一次校验启动早于 RAW_INVENTORY 写完，读取旧 SHA256SUMS 与写入中的 inventory 不一致；未提交这份中间状态。等归档写入完成后重新校验及 ZIP 逐项 SHA 校验均通过。

最终候选验收无 ERROR/UNKNOWN/MISSING/UNVERIFIED；准备阶段的上述编译失败、覆盖失败、后端异常及未单独保存的编译输出均已明确披露，不能当作 EQY 不等价或最终 PASS 项。

## 修改文件与工作树

仅新增本报告及 `c_b8368b0_evidence/` 的 C 驱动、夹具、摘要、报告和清单；完整文件列表见 FILES_CHANGED.txt。生产 pass、techlibs、冻结接口、Analyzer、官方 RTL、测试与 oracle 的 diff 为零。构建 .so 和完整原始输出在 ignored reports 目录并进入完整证据 ZIP。

提交和推送完成后的完整 HEAD、远端 HEAD、工作树状态及 ZIP SHA-256 记录于交付目录 DELIVERY_VERIFICATION.json。仅推送 C 分支，不合并主分支。MANIFEST.md 解释原始输出位置、完整性清单和复核命令。
''')
# Raw inventory hashes every output file; preserved setup attempts are included, not silently dropped.
inv=O/'RAW_INVENTORY.jsonl';count=total=0
with inv.open('w') as out:
 for p in sorted(O.rglob('*')):
  if p.is_file() and p!=inv:
   n=p.stat().st_size;out.write(json.dumps({'path':str(p.relative_to(R)),'size':n,'sha256':sha(p)},ensure_ascii=False)+'\n');count+=1;total+=n
   if count%25000==0:print('INVENTORY',count,flush=True)
(E/'raw_inventory_summary.json').write_text(json.dumps({'files':count,'bytes':total,'inventory':str(inv.relative_to(R)),'sha256':sha(inv)},indent=2)+'\n')
files=sorted([report,*[p for p in E.iterdir() if p.is_file() and p.name!='SHA256SUMS']])
# MANIFEST/FILENAME list created before final checksum list; git commit binds SHA256SUMS.
(E/'MANIFEST.md').write_text(f'''# C b8368b0 evidence manifest

Candidate B: `{B}`. Authorization A: `{A}`. Verdict: G3 PASS.

Small tracked evidence: this directory and ../C_DEFAULT_PATH_FINAL_ACCEPTANCE_b8368b0.md.
Full raw evidence: `pmux_project/reports/c-b8368b0-default-final/` in the C worktree and delivery ZIP. Raw files: {count}; bytes: {total}. RAW_INVENTORY.jsonl SHA-256: `{sha(inv)}`. Every raw file (including setup attempts, executable copies, build logs, native netlists, stats JSON, EQY configs and complete workdirs, 2000 seed rows and logs) has a content hash and size in that JSONL.

Main commands: `python3 build_c.py`, `python3 verify_frozen.py`, `python3 native_acceptance.py`, `python3 regress_c.py`, `python3 run_lifecycle.py`, `python3 run_core.py`, `python3 finalize_c.py`, run in this evidence directory in the exact B checkout before the C evidence commit. Use a new worktree/new empty output root to reproduce; drivers intentionally refuse existing final output directories. Build with fixed toolchain paths in frozen_provenance.json. Actual external commands are stored in commands.txt, raw per-run command.txt/command*.json and run.ys; seed wrappers also emit their actual arguments. Inspect per-run environment.json and toolchain.txt.

SHA256SUMS uses repository-relative paths. From repository root run `sha256sum -c pmux_project/experiments/case34_v02b/c_b8368b0_evidence/SHA256SUMS`. It includes all tracked C evidence and the complete raw inventory, selected official gold/default gates, final nets and whole-design EQY PASS markers. Use RAW_INVENTORY.jsonl to verify every remaining raw file. The checksum file excludes itself to avoid circular hashes; its Git object is bound by the pushed C commit. Delivery ZIP and commit hashes are recorded externally in DELIVERY_VERIFICATION.json.

Lifecycle observation fields in JSON are mode, authorization, payload_equal, lifecycle_valid, lifecycle_monitor_match. Profiles and reasons refer to actual consumer logs; pre_effect_four_field_assertions confirms real techmap/ABC9 boundary checks.

Preparation-stage failures are explicitly detailed in the final report and preserved in lifecycle_setup_attempt_* directories; they are excluded from final 58/58. A temporary protected-API compile error lacks a separate saved log, explicitly disclosed; it was not a required gate run. No candidate failure is hidden.
''')
files=sorted([report,*[p for p in E.iterdir() if p.is_file() and p.name not in ['SHA256SUMS','FILES_CHANGED.txt']]])
(E/'FILES_CHANGED.txt').write_text('\n'.join(str(p.relative_to(R)) for p in files+[E/'FILES_CHANGED.txt',E/'SHA256SUMS'])+'\n')
files+= [E/'FILES_CHANGED.txt',inv]
for i in range(1,5):files += [O/f'official/test{i}.v',O/f'native/case{i}/default/post_abc9.il',O/f'native/case{i}/default/final.il',O/f'native/case{i}/eqy/work/PASS']
(E/'SHA256SUMS').write_text('\n'.join(f'{sha(p)}  {p.relative_to(R)}' for p in sorted(set(files)))+'\n')
print('FINAL_AUDIT_PASS',count,total,flush=True)