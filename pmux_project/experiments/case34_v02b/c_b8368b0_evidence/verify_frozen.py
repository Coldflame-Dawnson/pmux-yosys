#!/usr/bin/env python3
import hashlib,json,os,platform,subprocess
from pathlib import Path
E=Path(__file__).resolve().parent;R=E.parents[3];O=R/'pmux_project/reports/c-b8368b0-default-final';T=Path('/home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
expected={'yosys-v0.69-src':'9f75ca1f9834a39a863915b5dae0c7b1e33533bc','eqy-v0.69-src':'4a72eb94fc253062464afee4d0018359017bb846','sby-v0.69-src':'b1a1e98cba941ec8433f8dc27f416cd7bb7f14be','yosys-v0.69-src/abc':'a51bf4a7bb38ce06ea3a544b1463b40832213917'}
actual={p:subprocess.check_output(['git','-C',str(T.parent/p),'rev-parse','HEAD'],text=True).strip() for p in expected};assert actual==expected,(actual,expected)
original=Path('/mnt/c/Users/15091/Desktop/Yosys_简单电路全流程例子_朱跃文/比赛要求/pmux_case/competition_case')
inputs=[]
for i in range(1,5):
 a=original/f'test{i}.v';b=O/'official'/f'test{i}.v';assert sha(a)==sha(b)
 inputs.append({'case':i,'original':str(a),'gold':str(b),'sha256':sha(a),'same':True})
executables={str(p):sha(p) for p in [*(T/'bin'/n for n in ['yosys','yosys-abc','eqy','sby']),O/'runtime/bin/yosys',O/'runtime/bin/yosys-abc']}
assert sha(T/'bin/yosys')==sha(O/'runtime/bin/yosys')
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=R,text=True).strip()=='b8368b0f3e2d55a2bb429296a3aea859eb4f2049'
assert subprocess.check_output(['git','rev-parse','HEAD^'],cwd=R,text=True).strip()=='ec86209a61f22020707b299f5175f02b8d71495d'
auth=subprocess.check_output(['git','show','d03381f4511ab520372f3c2e57657b141a4959bc:pmux_project/analyzer/generation_monitor_reaudit_b8368b0.md'],cwd=R,text=True);assert '\n`AUTHORIZED`\n' in auth
(E/'a_authorization_readonly.md').write_text(auth)
record={'B':'b8368b0f3e2d55a2bb429296a3aea859eb4f2049','parent':'ec86209a61f22020707b299f5175f02b8d71495d','A_HEAD':'d03381f4511ab520372f3c2e57657b141a4959bc','A_verdict':'AUTHORIZED','tool_sources':actual,'executables_sha256':executables,'inputs':inputs,'platform':platform.platform(),'python':platform.python_version(),'pwd':str(R),'tool_root':str(T),'runtime':str(O/'runtime'),'timezone':'Asia/Shanghai','PATH':os.environ.get('PATH'),'uname':subprocess.check_output(['uname','-a'],text=True).strip(),'tool_versions':{n:subprocess.check_output([str(T/'bin'/n),'-V' if n=='yosys' else '--version'],text=True).strip() for n in ['yosys','eqy','sby']}}
(E/'frozen_provenance.json').write_text(json.dumps(record,indent=2,ensure_ascii=False)+'\n');print('FROZEN_TOOLS_INPUTS_AUTHORIZATION_PASS',flush=True)