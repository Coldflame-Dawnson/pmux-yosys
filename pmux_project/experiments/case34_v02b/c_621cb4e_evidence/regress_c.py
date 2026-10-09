#!/usr/bin/env python3
"""Run the unchanged frozen C seed checker per seed, concurrently; aggregate every result."""
import concurrent.futures,hashlib,json,shlex,subprocess,time
from pathlib import Path
R=Path('/home/yuewen/work/pmux-yosys-c-621cb4e');E=Path(__file__).parent;O=R/'pmux_project/reports/c-621cb4e-default-final';T=Path('/home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/bin')
seeds=[int(s) for s in (R/'pmux_project/tests/pmux_m1/seeds_1000.txt').read_text().splitlines() if s.strip()];assert len(seeds)==len(set(seeds))==1000
P=R/'pmux_project/pass/pmux_opt.so'
def template(mode):return f'python3 {E}/dut_adapter.py --before {{before}} --after {{after}} --plan {{plan}} --stats {{stats}} --plugin {P} --yosys {T}/yosys'+(' --explicit' if mode=='explicit' else '')
cmd=['python3',R/'pmux_project/scripts/run_micro.py','--mode','external','--external-kind','real','--dut-template',template('default'),'--bin',T,'--out',O/'micro']
with (E/'commands.txt').open('a') as f:f.write(shlex.join(list(map(str,cmd)))+'\n')
with (O/'micro_driver.log').open('w') as f:cp=subprocess.run(list(map(str,cmd)),stdout=f,stderr=subprocess.STDOUT,cwd=R)
print('MICRO_RC',cp.returncode,flush=True)
if cp.returncode:raise SystemExit('BLOCKED micro')
def work(mode,seed):
    out=O/'random'/mode/f'input_{seed:08d}';out.mkdir(parents=True,exist_ok=False)
    cmd=['python3',R/'pmux_project/scripts/run_random.py','--seed',seed,'--mode','external','--external-kind','real','--dut-template',template(mode),'--bin',T,'--out',out]
    (out/'command.txt').write_text(shlex.join(list(map(str,cmd)))+'\n')
    with (out/'driver.log').open('w') as f:cp=subprocess.run(list(map(str,cmd)),stdout=f,stderr=subprocess.STDOUT,cwd=R)
    summary=out/'summary.json';doc=json.loads(summary.read_text()) if summary.is_file() else {'rows':[],'pass':0,'fail':1}
    return {'mode':mode,'seed':seed,'rc':cp.returncode,'pass':doc.get('pass')==1 and cp.returncode==0,'summary':str(summary),'row':doc['rows'][0] if doc['rows'] else {}}
started=time.monotonic();rows=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
    futures=[pool.submit(work,mode,seed) for mode in ['default','explicit'] for seed in seeds]
    for future in concurrent.futures.as_completed(futures):
        row=future.result();rows.append(row)
        if len(rows)%100==0:print('RANDOM_PROGRESS',len(rows),sum(r['pass'] for r in rows),'elapsed',round(time.monotonic()-started),flush=True)
        if not row['pass']:
            (E/'random_first_failure.json').write_text(json.dumps(row,indent=2));print('BLOCKED_RANDOM',row,flush=True)
rows.sort(key=lambda r:(r['mode'],r['seed']))
summary={'seed_inventory_sha256':hashlib.sha256((R/'pmux_project/tests/pmux_m1/seeds_1000.txt').read_bytes()).hexdigest(),'workers':8,'wall_seconds':time.monotonic()-started,'groups':{mode:{'passed':sum(r['pass'] for r in rows if r['mode']==mode),'checks':sum(r['mode']==mode for r in rows)} for mode in ['default','explicit']},'rows':rows}
(O/'random_summary.json').write_text(json.dumps(summary,indent=2)+'\n');(E/'random_summary.json').write_text(json.dumps({k:v for k,v in summary.items() if k!='rows'},indent=2)+'\n')
print('RANDOM_SUMMARY',summary['groups'],flush=True)
raise SystemExit(0 if all(r['pass'] for r in rows) else 1)