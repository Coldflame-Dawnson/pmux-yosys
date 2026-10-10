#!/usr/bin/env python3
"""C direct native-default acceptance, fresh artifacts and frozen whole-design EQY."""
import csv,hashlib,json,os,re,shlex,subprocess,time
from pathlib import Path
R=Path('/home/yuewen/work/pmux-yosys-c-b8368b0'); O=R/'pmux_project/reports/c-b8368b0-default-final'; E=Path(__file__).parent
T=Path('/home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install')
ENV=dict(os.environ,YOSYS_DATDIR=str(O/'data')); ENV['PATH']=str(T/'bin')+os.pathsep+ENV.get('PATH','')
P=R/'pmux_project/pass/pmux_opt.so'; N=O/'build/native_synth.so'; Y=O/'runtime/bin/yosys'
def run(cmd,log):
    log.parent.mkdir(parents=True,exist_ok=True)
    with (E/'commands.txt').open('a') as f:f.write(shlex.join(list(map(str,cmd)))+'\n')
    start=time.monotonic()
    with log.open('w') as f:cp=subprocess.run(list(map(str,cmd)),stdout=f,stderr=subprocess.STDOUT,env=ENV,cwd=R,timeout=900)
    return cp.returncode,time.monotonic()-start

def ys(commands,d,extra=()):
    d.mkdir(parents=True,exist_ok=True); script=d/'run.ys';script.write_text(';\n'.join(commands)+';\n')
    cmd=[Y,'-Q','-m',P,'-m',N,*extra,'-s',script,'-l',d/'yosys.log']
    rc,wall=run(['/usr/bin/time','-v','-o',d/'resources.txt',*cmd],d/'console.log')
    return rc,wall,(d/'yosys.log').read_text()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def counts(p,top):
    m=json.loads(p.read_text())['modules'][top]; types={}
    for c in m['cells'].values():types[c['type']]=types.get(c['type'],0)+1
    return {'lut':types.get('$lut',0),'dff':types.get('dffeas',0),'io':sum(len(v['bits']) for v in m['ports'].values()),'cells':len(m['cells']),'types':types}
def profile(log):
    return re.findall(r'synth_intel: pmux_profile=(\w+)',log)[0]
# Freeze EQY predicate verbatim from B validation function.
import importlib.util
spec=importlib.util.spec_from_file_location('b_validation',R/'pmux_project/experiments/case34_v02b/scripts/run_production_validation.py'); mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
expect=[(288,222,288,222,'stock'),(25,0,25,0,'stock'),(13,17,11,12,'stock'),(36,10,27,10,'accepted_constant_table')]
results=[]
for index,(bl,bd,fl,fd,prof) in enumerate(expect,1):
    top=f'test{index}'; source=O/'official'/f'{top}.v'; row={'case':f'case{index}','gold_sha256':sha(source)}
    for variant in ['baseline','default']:
        d=O/'native'/f'case{index}'/variant
        option=' -no-pmux-v02' if variant=='baseline' else ''
        commands=[f'read_verilog {source}',f'synth_intel -family cycloneiv -top {top}{option} -run begin:map_cells','scratchpad -assert pmux_opt.accepted_constant_table false','scratchpad -assert pmux_opt.accepted_constant_table_snapshot \"\"','scratchpad -assert pmux_opt.accepted_constant_table_generation \"\"','scratchpad -assert pmux_opt.accepted_constant_table_lifecycle_valid false','check -noinit','stat',f'write_json {d}/post_abc9.json',f'select {top}',f'write_rtlil -selected {d}/post_abc9.il','select *',f'synth_intel -family cycloneiv -top {top} -run map_cells:',f'select {top}',f'write_rtlil -selected {d}/final.il']
        rc,wall,log=ys(commands,d)
        record={'rc':rc,'wall_seconds':wall,'profile':profile(log)}
        if rc: row[variant]=record;results.append(row);(E/'native_results.json').write_text(json.dumps(results,indent=2));raise SystemExit('BLOCKED: native synthesis failed '+str(d))
        record.update(counts(d/'post_abc9.json',top));record['gate_sha256']=sha(d/'post_abc9.il');record['final_sha256']=sha(d/'final.il')
        edits=re.findall(r'v02_case34 .*?committed=(\d+)',log);record['structural_edits']=sum(map(int,edits))
        resources=(d/'resources.txt').read_text();record['user_seconds']=float(re.search(r'User time \(seconds\): ([\d.]+)',resources)[1]);record['system_seconds']=float(re.search(r'System time \(seconds\): ([\d.]+)',resources)[1]);record['peak_kib']=int(re.search(r'Maximum resident set size \(kbytes\): (\d+)',resources)[1])
        record['abc_time_lines']=[line for line in log.splitlines() if 'ABC:' in line and ('Time' in line or 'time' in line)]
        row[variant]=record
    d=O/'native'/f'case{index}'/'eqy';d.mkdir(parents=True,exist_ok=True)
    cfg=d/'verify.eqy';cfg.write_text(mod.eqy_config(source,O/'native'/f'case{index}'/'default/post_abc9.il',top,R/'pmux_project/official_baseline/environment/dffeas_formal.v'))
    rc,wall=run([T/'bin/eqy','-f','-d',d/'work',cfg],d/'eqy.log');row['eqy']='PASS' if rc==0 and (d/'work/PASS').is_file() else 'FAIL';row['eqy_wall_seconds']=wall
    row['checks_pass']=(row['baseline']['lut'],row['baseline']['dff'],row['default']['lut'],row['default']['dff'],row['default']['profile'])==(bl,bd,fl,fd,prof) and row['eqy']=='PASS' and row['default']['io']==row['baseline']['io'] and row['default']['structural_edits']==(1 if index>=3 else 0)
    results.append(row);(E/'native_results.json').write_text(json.dumps(results,indent=2)+'\n')
    print(row['case'],row['default']['lut'],row['default']['dff'],row['default']['profile'],row['eqy'],row['checks_pass'],flush=True)
    if not row['checks_pass']:raise SystemExit('BLOCKED: native frozen acceptance mismatch')
with (E/'results.csv').open('w',newline='') as f:
    fields=['case','baseline_lut','baseline_dff','lut','dff','io','io_delta','total_cells','structural_edits','profile','eqy','gold_sha256','gate_sha256','cpu_seconds','peak_kib']
    w=csv.DictWriter(f,fieldnames=fields);w.writeheader()
    for x in results:
        b,v=x['baseline'],x['default'];w.writerow(dict(case=x['case'],baseline_lut=b['lut'],baseline_dff=b['dff'],lut=v['lut'],dff=v['dff'],io=v['io'],io_delta=v['io']-b['io'],total_cells=v['cells'],structural_edits=v['structural_edits'],profile=v['profile'],eqy=x['eqy'],gold_sha256=x['gold_sha256'],gate_sha256=v['gate_sha256'],cpu_seconds=v['user_seconds']+v['system_seconds'],peak_kib=v['peak_kib']))
print('NATIVE_FOUR_CASE_PASS',flush=True)