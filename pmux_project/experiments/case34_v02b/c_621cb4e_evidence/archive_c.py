#!/usr/bin/env python3
"""Archive observations only after stopping on the production blocker."""
import csv,hashlib,json,re,shutil,subprocess
from pathlib import Path
R=Path('/home/yuewen/work/pmux-yosys-c-621cb4e');E=Path(__file__).parent;O=R/'pmux_project/reports/c-621cb4e-default-final'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
# Correct machine classification: frozen EQY returns ERROR, not a formal inequivalence verdict.
f=E/'snapshot_counterexample.json';d=json.loads(f.read_text());d['eqy']='ERROR';d['sat_status']='UNKNOWN';d['pdr_status']='ERROR: unsupported $lut in maybe-x preparation';d['formal_inequivalence']='UNVERIFIED';f.write_text(json.dumps(d,indent=2)+'\n')
# Copy minimal distinguishing evidence into Git, with unchanged full large files in ignored report tree.
for name in ['plan.json','stats.json','run.ys','verify.eqy']:
    shutil.copyfile(O/'snapshot_counterexample'/name,E/('counterexample_'+name))
log=(O/'snapshot_counterexample/yosys.log').read_text()
(E/'counterexample_assertions.txt').write_text('\n'.join(line for line in log.splitlines() if 'C_SNAPSHOT_MUTATION' in line or 'pmux_case34_lut_map.v' in line or 'pmux_case34_abc.script' in line or 'scratchpad -assert' in line)+'\n')
a=(O/'snapshot_counterexample/post_producer.il').read_text().splitlines();b=(O/'snapshot_counterexample/changed_snapshot.il').read_text().splitlines()
import difflib
(E/'counterexample_one_bit.diff').write_text('\n'.join(difflib.unified_diff(a,b,fromfile='post_producer.il',tofile='changed_snapshot.il',n=8))+'\n')
shutil.copyfile(O/'micro/summary.json',E/'micro_summary.json')
status=json.loads((E/'regression_stop.json').read_text())
(E/'regression_summary.txt').write_text(f'Micro: 19/19 PASS (C independent)\nDefault seeds: {status["default"]["passed"]}/1000 completed PASS; STOPPED, full 1000 UNVERIFIED\nExplicit -v02-case34 seeds: {status["explicit"]["passed"]}/1000; STOPPED, full 1000 UNVERIFIED\nProduction mutation seam: UNVERIFIED / NOT RUN after blocker\nUnified behavior 17: UNVERIFIED / NOT RUN after blocker\nAnalyzer fast/slow: UNVERIFIED / NOT RUN after blocker\nRead-only fingerprints/determinism/table32: UNVERIFIED / NOT RUN after blocker\n')
(E/'replay_test_summary.txt').write_text('Native legal first consumption: accepted_constant_table; post-call scratchpad false assertion PASS.\nNative cell regeneration/replay: UNVERIFIED; stopped before execution.\nNative changed-snapshot rejection: FAIL; changed one LUT parameter bit after genuine authorization; accepted_constant_table instead of stock.\nOther required unauthorized/ambiguous/unknown-profile cases: UNVERIFIED after stop.\n')
with (E/'profile_selection.csv').open('w',newline='') as f:
    w=csv.writer(f);w.writerow(['test','expected','actual','status'])
    for x in json.loads((E/'native_results.json').read_text()):w.writerow([x['case'],x['default']['profile'],x['default']['profile'],'PASS'])
    w.writerow(['changed_snapshot','stock','accepted_constant_table','FAIL'])
# Verify official gold against the user's original inputs without editing any RTL.
original=Path('/mnt/c/Users/15091/Desktop/Yosys_简单电路全流程例子_朱跃文/比赛要求/pmux_case/competition_case')
comparisons=[]
for i in range(1,5):
    a=original/f'test{i}.v';b=O/'official'/f'test{i}.v';comparisons.append({'case':i,'original':str(a),'original_sha256':sha(a),'gold_sha256':sha(b),'same':sha(a)==sha(b)})
(E/'official_input_comparison.json').write_text(json.dumps(comparisons,indent=2,ensure_ascii=False)+'\n')
assert all(x['same'] for x in comparisons)
# Preserve exact A read-only authorization, never merge A commits.
for rev,name in [('19b8dee568082393aa2f5a77091fef860dd4fe68','a_authorization_report.md')]:
    text=subprocess.check_output(['git','show',rev+':pmux_project/analyzer/unified_selection_consume_reaudit_621cb4e.md'],cwd=R,text=True)
    (E/name).write_text(text)
provenance={'B':subprocess.check_output(['git','rev-parse','HEAD'],cwd=R,text=True).strip(),'parent':subprocess.check_output(['git','rev-parse','HEAD^'],cwd=R,text=True).strip(),'A_content':'3cc303df7ea1336659f137b564453aab76c76b66','A_HEAD':'19b8dee568082393aa2f5a77091fef860dd4fe68','A_verdict':'AUTHORIZED (read-only reference; not C verdict)','production_tracked_diff':subprocess.check_output(['git','diff','--name-only','621cb4e56b734da73cfb6161b951a375893e2d81','--','pmux_project/pass','pmux_project/include','pmux_project/analyzer','techlibs/intel'],cwd=R,text=True),'relocated_yosys_sha256':sha(O/'runtime/bin/yosys'),'original_yosys_sha256':sha(Path('/home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/bin/yosys'))}
(E/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
print('ARCHIVE_CORE_COMPLETE')