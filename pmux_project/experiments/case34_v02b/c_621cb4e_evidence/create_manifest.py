#!/usr/bin/env python3
"""Create checksummed C archive manifest; never classify missing results as PASS."""
import hashlib,json,os,subprocess
from pathlib import Path
E=Path(__file__).resolve().parent;R=E.parents[3];O=R/'pmux_project/reports/c-621cb4e-default-final';report=E.parent/'C_DEFAULT_PATH_FINAL_ACCEPTANCE_621cb4e.md'
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
def role(p):
 s=str(p.relative_to(O))
 if s.startswith('random/'):
  return ('单 seed 冻结回归命令、输入、结构/check/EQY 结果与工具生成物','regress_c.py -> unchanged run_random.py --seed; see sibling command.txt','frozen seeds_1000.txt, generate_micro/Design, B plugin, fixed tools')
 if s.startswith('native/'):
  return ('官方 baseline/default 直接默认综合及 whole-design EQY 证据','native_acceptance.py; see run.ys or verify.eqy','official/test[1-4].v, unchanged B synth_intel and plugin, frozen tools and mappings')
 if s.startswith('snapshot_counterexample/'):
  return ('单参数位快照变化反例阶段、映射或冻结 EQY 证据','snapshot_counterexample.py; see run.ys and verify.eqy','official/test4.v, B producer, unchanged native pass, C producer_observer.so')
 if s.startswith('micro/'):
  return ('19 微测试冻结回归证据','regress_c.py -> unchanged run_micro.py; see *.command.txt','frozen pmux_m1 micro manifest, B plugin and fixed tools')
 if s.startswith('runtime/') or s.startswith('data/'):
  return ('独立运行目录：固定工具/数据副本与 B 映射资产','build_c.py (original setup via prepare_runtime.py archived)','fixed toolchain/install and B techlibs/intel/common')
 if s.startswith('setup_probe_'):
  return ('首次错误数据目录的环境探测证据；不计为最终 gate','initial native_acceptance.py before runtime relocation','fixed original installed yosys, missing PMUX mapping asset')
 if s.startswith('official/'):
  return ('本轮 gold 输入只读副本','build_c.py copy + archive_c.py hash comparison','original pmux_case/competition_case')
 if s.startswith('build/'):
  return ('本轮 C native loader/observer 动态库与构建生成物','yosys-config --build; commands.txt / report repro','C fixture sources, unchanged B synth_intel, fixed toolchain headers')
 return ('环境、构建、命令或控制日志','commands.txt; build_c.py / runtime preparation / regression driver','fixed B checkout, toolchain, C acceptance scripts')
# Full per-file inventory stays with the ignored heavy evidence. No self checksum is possible.
inv=O/'artifact_inventory.jsonl';n=0;size=0
with inv.open('w') as f:
 for p in sorted(O.rglob('*')):
  if not p.is_file() or p==inv:continue
  purpose,cmd,deps=role(p)
  item={'path':str(p.relative_to(R)),'purpose':purpose,'command':cmd,'inputs':deps,'sha256':sha(p),'git':False,'bytes':p.stat().st_size}
  f.write(json.dumps(item,ensure_ascii=False)+'\n');n+=1;size+=item['bytes']
summary={'files':n,'bytes':size,'inventory':str(inv.relative_to(R)),'inventory_sha256':sha(inv),'production_diff':subprocess.check_output(['git','diff','--name-only','621cb4e56b734da73cfb6161b951a375893e2d81','--','pmux_project/pass','pmux_project/include','pmux_project/analyzer','techlibs/intel'],cwd=R,text=True).strip(),'missing':['full default seed 1000 aggregate PASS','explicit seed 1000 aggregate PASS','production mutation 26 results','unified behavior 17 results','Analyzer fast/slow results','regenerated-cell replay logs','read-only/determinism/table32 independent results']}
(E/'archive_summary.json').write_text(json.dumps(summary,indent=2)+'\n')
# Every tracked evidence file except circular manifest/checksum/index documents is individually listed.
files=sorted([report,*[p for p in E.iterdir() if p.is_file() and p.name not in ('MANIFEST.md','SHA256SUMS','modified_files.txt')]])
lines=['# C evidence manifest','', '状态：BLOCKED；唯一已确认阻塞为授权后 marker LUT 参数变更仍选择 accepted_constant_table。','',
'Git 中保留报告、C 测试脚本/夹具和小型摘要；完整逐文件机器清单 artifact_inventory.jsonl 保存在 Git 忽略报告目录，其 SHA-256 由 archive_summary.json 和 SHA256SUMS 绑定。机器清单为每个大型生成物列出用途、生成命令、输入依赖、哈希和 Git=false。所有未执行结论是 UNVERIFIED；下列预期结果文件未生成，为 MISSING。','',
'| 预期材料 | 状态 |','|---|---|']
for m in summary['missing']:lines.append('| '+m+' | MISSING / UNVERIFIED（按用户要求在生产缺陷出现后停止） |')
lines+=['','## Git 跟踪文件','', '| 文件 | 用途 | 生成命令 | 输入依赖 | SHA-256 | Git |','|---|---|---|---|---|---|']
for p in files:
 name=p.name
 if name.endswith('.py') or name.endswith('.cc'):purpose='C 验收/归档脚本或测试夹具，非生产实现';cmd='C 创建；编译/执行见 commands.txt 和完整报告';deps='固定 B 源码、冻结工具/测试接口与本轮任务要求'
 elif name=='a_authorization_report.md':purpose='A 只读授权材料，非 C 判定';cmd='git show 19b8dee...:pmux_project/analyzer/unified_selection_consume_reaudit_621cb4e.md';deps='A HEAD 19b8dee...'
 elif 'counterexample' in name or 'snapshot' in name:purpose='最小单参数位变更反例配置/摘要';cmd='snapshot_counterexample.py + archive_c.py';deps='C native 故障注入运行及冻结 EQY 配置'
 elif name==report.name:purpose='C 最终 BLOCKED 验收报告';cmd='finish_report.py';deps='本轮 native/反例/微测试/停止回归证据'
 else:purpose='本轮环境/资源/profile/回归/归档摘要';cmd='build_c.py / native_acceptance.py / archive_c.py / create_manifest.py';deps='本轮原始日志和固定输入；详见完整报告'
 lines.append('| '+str(p.relative_to(R))+' | '+purpose+' | '+cmd+' | '+deps+' | `'+sha(p)+'` | yes |')
lines+=['','## 大型生成物','',f'- 文件数：{n}；字节数：{size}。',f'- 逐文件 inventory：`{inv.relative_to(R)}`。',f'- inventory SHA-256：`{sha(inv)}`。','- native/case[1-4]/baseline 与 default：post-ABC9 JSON/RTLIL、最终 RTLIL、完整日志、run.ys、CPU/RSS。','- native/case[1-4]/eqy：冻结配置、完整日志、工作目录 PASS 标记。','- snapshot_counterexample：真实 producer 与变更后的阶段 RTLIL、映射 gate、完整 EQY 工作目录，EQY 总状态 ERROR；正式不等价 UNVERIFIED。','- random/default：582 个完成的种子，以及停止时未完成样本；random/explicit 未执行。','- MANIFEST.md 和 SHA256SUMS 的自哈希不可自包含；MANIFEST.md 由 SHA256SUMS 绑定，SHA256SUMS 由最终 Git commit 绑定。modified_files.txt 为文件名索引，Git commit 绑定。']
(E/'MANIFEST.md').write_text('\n'.join(lines)+'\n')
checks=[*files,E/'MANIFEST.md',inv]
# Track selected large evidence hashes directly; remaining files are bound by full inventory digest.
for p in sorted((O/'native').rglob('*')):
 if p.is_file() and (p.name in ('PASS','post_abc9.json','post_abc9.il','final.il','yosys.log','verify.eqy','eqy.log','resources.txt','run.ys')):checks.append(p)
for p in sorted((O/'snapshot_counterexample').glob('*')):
 if p.is_file():checks.append(p)
(E/'SHA256SUMS').write_text('\n'.join(sha(p)+'  '+str(p.relative_to(R)) for p in checks)+'\n')
(E/'modified_files.txt').write_text('\n'.join(str(p.relative_to(R)) for p in sorted([report,*[p for p in E.iterdir() if p.is_file()]]))+'\n')
print(json.dumps({'files':n,'bytes':size,'tracked_files':len(list(E.iterdir()))+1},indent=2))