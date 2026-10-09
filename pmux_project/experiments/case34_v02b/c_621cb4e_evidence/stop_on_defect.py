import os,signal,subprocess,json,time
from pathlib import Path
r=Path('/home/yuewen/work/pmux-yosys-c-621cb4e');e=r/'pmux_project/experiments/case34_v02b/c_621cb4e_evidence';o=r/'pmux_project/reports/c-621cb4e-default-final'
rows=[]
for line in subprocess.check_output(['ps','-eo','pid=,ppid=,args='],text=True).splitlines():
    parts=line.strip().split(None,2)
    if len(parts)==3:rows.append((int(parts[0]),int(parts[1]),parts[2]))
roots=[pid for pid,ppid,args in rows if args.startswith('python3 ') and args.endswith('/c_621cb4e_evidence/regress_c.py')]
targets=set(roots)
while True:
    new={pid for pid,ppid,args in rows if ppid in targets}
    if new<=targets:break
    targets|=new
for pid in sorted(targets,reverse=True):
    try:os.kill(pid,signal.SIGTERM)
    except ProcessLookupError:pass
status={'reason':'Stop required by user after native snapshot-profile production defect','terminated_root_pids':roots,'terminated_process_count':len(targets)}
for mode in ['default','explicit']:
    files=list((o/'random'/mode).glob('input_*/summary.json'));docs=[json.loads(f.read_text()) for f in files]
    status[mode]={'completed':len(files),'passed':sum(x.get('pass',0) for x in docs),'failed':sum(x.get('fail',0) for x in docs),'required':1000,'status':'INCOMPLETE_STOPPED_ON_PRODUCTION_DEFECT'}
(e/'regression_stop.json').write_text(json.dumps(status,indent=2)+'\n');print(json.dumps(status,indent=2))