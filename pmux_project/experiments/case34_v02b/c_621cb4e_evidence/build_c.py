#!/usr/bin/env python3
"""C isolated build: compile unchanged B production sources, replace stock registration only."""
import hashlib,json,os,shutil,subprocess
from pathlib import Path
R=Path('/home/yuewen/work/pmux-yosys-c-621cb4e')
O=R/'pmux_project/reports/c-621cb4e-default-final'
T=Path('/home/yuewen/work/pmux-yosys/pmux_project/official_baseline/toolchain/install')
E=R/'pmux_project/experiments/case34_v02b/c_621cb4e_evidence'
O.mkdir(parents=True,exist_ok=False)
(O/'build').mkdir()
def run(cmd,name):
    with (O/(name+'.log')).open('w') as f:
        cp=subprocess.run(list(map(str,cmd)),cwd=R,stdout=f,stderr=subprocess.STDOUT)
    with (E/'commands.txt').open('a') as f:
        import shlex
        f.write(shlex.join(list(map(str,cmd)))+'\n')
    if cp.returncode: raise RuntimeError(name)
    return (O/(name+'.log')).read_text()
run(['git','rev-parse','HEAD'],'start_head')
run(['git','status','--short','--branch'],'start_status')
run(['git','remote','-v'],'start_remote')
run(['git','merge-base','--is-ancestor','9107a466f74ce27269361ee26e99093c1b8fc93e','621cb4e56b734da73cfb6161b951a375893e2d81'],'start_ancestor')
run(['git','rev-parse','HEAD^'],'start_parent')
(E/'toolchain.txt').write_text(run([T/'bin/yosys','-V'],'yosys_version')+run([T/'bin/eqy','--version'],'eqy_version')+run([T/'bin/sby','--version'],'sby_version')+run(['git','-C',T.parent/'yosys-v0.69-src','rev-parse','HEAD'],'yosys_source')+run(['git','-C',T.parent/'eqy-v0.69-src','rev-parse','HEAD'],'eqy_source')+run(['git','-C',T.parent/'sby-v0.69-src','rev-parse','HEAD'],'sby_source'))
# Registration shim runs at dlopen, before plugin Pass::init_register; production source is included byte unchanged.
shim=E/'native_synth_loader.cc'
shim.write_text('#include "kernel/register.h"\nnamespace { struct CReplaceStock { CReplaceStock() { Yosys::pass_register.erase("synth_intel"); } } c_replace_stock; }\n#include "../../../../techlibs/intel/synth_intel.cc"\n')
run([T/'bin/yosys-config','--build',O/'build/native_synth.so',shim],'native_build')
run(['make','-C','pmux_project/pass',f'YOSYS_CONFIG={T}/bin/yosys-config'],'pass_build')
run(['make','-C','pmux_project/analyzer',f'YOSYS_ROOT={T}'],'analyzer_build')
# Private runtime data: stock data plus exact B mapping assets. Original installation stays read-only.
shutil.copytree(T/'share/yosys',O/'data',symlinks=True)
for name in ['pmux_case34_lut_map.v','pmux_case34_abc.script']:
    shutil.copyfile(R/'techlibs/intel/common'/name,O/'data/intel/common'/name)
source=Path('/home/yuewen/work/pmux-yosys/pmux_project/pmux_case/competition_case')
shutil.copytree(source,O/'official')
shutil.copytree(T/'bin',O/'runtime/bin',symlinks=True)
shutil.copytree(O/'data',O/'runtime/share/yosys',symlinks=True)
hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [*source.glob('test*.v'),T/'bin/yosys',T/'bin/yosys-abc',T/'bin/eqy',T/'bin/sby',R/'techlibs/intel/synth_intel.cc',R/'pmux_project/pass/pmux_opt.so',O/'build/native_synth.so',R/'pmux_project/analyzer/analyzer_selftest.so']}
(E/'initial_hashes.json').write_text(json.dumps(hashes,indent=2)+'\n')
print('C_BUILD_PASS',flush=True)