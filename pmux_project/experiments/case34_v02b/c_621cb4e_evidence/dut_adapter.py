#!/usr/bin/env python3
"""C explicit/default DUT adapter; no production code or expectation changes."""
import argparse,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser()
for n in ('before','after','plan','stats','plugin','yosys'):p.add_argument('--'+n,required=True,type=Path)
p.add_argument('--explicit',action='store_true');a=p.parse_args()
mode=' -v02-case34' if a.explicit else ''
s=f'read_rtlil {a.before}; pmux_opt{mode} -write-plan {a.plan} -write-stats {a.stats}; write_rtlil {a.after}'
a.after.parent.mkdir(parents=True,exist_ok=True)
(a.after.parent/'dut.ys').write_text(s+'\n')
env=dict(os.environ);env['PATH']=str(a.yosys.parent)+os.pathsep+env.get('PATH','')
cp=subprocess.run([str(a.yosys),'-Q','-m',str(a.plugin),'-s',str(a.after.parent/'dut.ys')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env)
(a.after.parent/'real_pass.log').write_text(cp.stdout)
raise SystemExit(cp.returncode)