#!/usr/bin/env python3
"""C independent two-consumer lifecycle/snapshot/one-shot matrix, with pre-effect assertions."""
import csv,json,os,re,shlex,subprocess,time
from pathlib import Path
E=Path(__file__).resolve().parent;R=E.parents[3];O=R/'pmux_project/reports/c-b8368b0-default-final';D=O/os.environ.get('C_LIFECYCLE_OUT','lifecycle');D.mkdir(exist_ok=False)
Y=O/'runtime/bin/yosys';base=[Y,'-Q','-m',R/'pmux_project/pass/pmux_opt.so','-m',O/'build/native_synth.so','-m',O/'build/lifecycle_fixture.so']
ENV=dict(os.environ);ENV['PATH']=str(Y.parent)+os.pathsep+ENV.get('PATH','')
clear=['scratchpad -assert pmux_opt.accepted_constant_table false','scratchpad -assert pmux_opt.accepted_constant_table_snapshot ""','scratchpad -assert pmux_opt.accepted_constant_table_generation ""','scratchpad -assert pmux_opt.accepted_constant_table_lifecycle_valid false']
def pre():return ['opt -fast -mux_undef -undriven -fine -full','memory_map','opt -undriven -fine']
def common():return ['opt_clean','techmap -map +/techmap.v','opt -full','clean -purge','setundef -undriven -zero']
def consume_runner():return [f'pmux_opt_abc9 -family cycloneiv -top test4 -cofactor-map {R}/pmux_project/pass/pmux_case34_lut_map.v -abc-script {R}/pmux_project/pass/pmux_case34_abc.script']
rows=[]
def run(path,mode,profiles):
 d=D/path/mode;d.mkdir(parents=True,exist_ok=False)
 prefix=[f'scratchpad -set c.path {path}',f'scratchpad -set c.mode {mode}',f'scratchpad -set c.stage_dir {d}',f'read_verilog {O}/official/test4.v']
 if path=='native':body=['synth_intel -family cycloneiv -top test4 -run begin:map_cells']
 else:body=['synth_intel -family cycloneiv -top test4 -run begin:map_ffram',*pre(),'pmux_opt',*common(),'c_apply_mutation','scratchpad -set c.effects_guard true',*consume_runner()]
 commands=prefix+body+clear+['c_cleanup_foreign',f'select test4',f'write_rtlil -selected {d}/post_consume.il']
 if mode.startswith('replay_'):
  # First genuine producer captures all target contents and the four originally published fields.
  commands=[c.replace(f'c.mode {mode}','c.mode capture') for c in commands]
  commands+=['select *','scratchpad -set c.effects_guard false','c_restore_targets'+(' old' if mode=='replay_old_auth' and path=='runner' else '')]
  if path=='native':commands += ['scratchpad -set c.mode '+('restore_old_after_producer' if mode=='replay_old_auth' else 'none'),'synth_intel -family cycloneiv -top test4 -run map_ffram:map_cells']
  else:commands += common()+['scratchpad -set c.effects_guard true']+consume_runner()
  commands+=clear+[f'select test4',f'write_rtlil -selected {d}/post_replay.il']
 script=d/'run.ys';script.write_text(';\n'.join(commands)+';\n');cmd=[*base,'-s',script,'-l',d/'yosys.log']
 with (E/'commands.txt').open('a')as f:f.write(shlex.join(map(str,cmd))+'\n')
 env=dict(ENV)
 if mode=='allocator_reuse':env['GLIBC_TUNABLES']='glibc.malloc.tcache_count=0'
 (d/'environment.json').write_text(json.dumps({'GLIBC_TUNABLES':env.get('GLIBC_TUNABLES'),'PATH':env['PATH']},indent=2)+'\n')
 with (d/'console.log').open('w')as f:cp=subprocess.run(map(str,cmd),cwd=R,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=180)
 log=(d/'yosys.log').read_text();marker=r'synth_intel: pmux_profile=(\w+)' if path=='native' else r'pmux_opt_abc9: profile=(\w+)'
 actual=re.findall(marker,log);reasons=re.findall(r'authorization rejected reason=(\w+)',log)
 obs=re.findall(r'C_BEFORE_CONSUME mode=(\S+) payload_equal=(\d+) authorized=(\d+) lifecycle_valid=(\d+) lifecycle_monitor_match=(\d+)',log)
 check=cp.returncode==0 and actual==profiles and 'C_CLEAR_BEFORE_EFFECT effect=techmap authorization=0 snapshot_empty=1 generation_empty=1 lifecycle_valid=0' in log and 'C_CLEAR_BEFORE_EFFECT effect=abc9' in log
 if mode in ['forge','forge_suffix','forge_numeric','old_generation','foreign_generation','unregister']:
  check=check and bool(obs) and obs[0][3:] == ('1','0') and 'target_lifecycle_mismatch' in reasons
 if mode in ['same_name_rebuild','allocator_reuse','module_rebuild','port_restore']:
  check=check and bool(obs) and obs[0][1]=='1' and obs[0][3]=='0' and 'target_lifecycle_mismatch' in reasons
 if mode=='allocator_reuse':check=check and 'pointer_reused=1' in log
 if mode.startswith('replay_'):check=check and 'C_RESTORED_FULL_TARGETS payload_equal=1' in log
 if mode=='replay_old_auth':check=check and 'target_lifecycle_mismatch' in reasons
 row={'path':path,'mode':mode,'rc':cp.returncode,'profiles':actual,'expected_profiles':profiles,'reasons':reasons,'observations':obs,'pre_effect_four_field_assertions':cp.returncode==0 and 'C_CLEAR_BEFORE_EFFECT' in log,'pass':bool(check),'log':str(d/'yosys.log')};rows.append(row)
 (E/'lifecycle_matrix.json').write_text(json.dumps({'passed':sum(r['pass'] for r in rows),'total':len(rows),'rows':rows},indent=2)+'\n')
 print(path,mode,'PASS' if check else 'FAIL',actual,reasons,flush=True)
 if not check:raise SystemExit('BLOCKED lifecycle: '+path+'/'+mode)
modes=['none','forge','forge_suffix','forge_numeric','empty_generation','lifecycle_false','old_generation','foreign_generation','unregister','second_generation','same_name_rebuild','module_rebuild','port_restore','allocator_reuse','lut_bit','lut_width','attribute','type','port_order','remove_one','remove_all','add_extra','drop_snapshot','auth_false','unknown_auth','stub_unknown','disable','replay_no_auth','replay_old_auth']
if os.environ.get('C_ONLY_ALLOCATOR'):modes=['allocator_reuse']
for path in ['native','runner']:
 for mode in modes:
  expected=['accepted_constant_table','stock'] if mode.startswith('replay_') else ['accepted_constant_table'] if mode in ['none','second_generation'] else ['stock']
  run(path,mode,expected)
# Explicit two-path agreement including rejection reasons.
for mode in modes:
 a,b=[r for r in rows if r['mode']==mode];assert a['profiles']==b['profiles'] and a['reasons']==b['reasons'],mode
with(E/'lifecycle_matrix.csv').open('w',newline='')as f:
 w=csv.writer(f,lineterminator='\n');w.writerow(['path','mode','profiles','reasons','pass'])
 for r in rows:w.writerow([r['path'],r['mode'],';'.join(r['profiles']),';'.join(r['reasons']),r['pass']])
print('C_LIFECYCLE_MATRIX',len(rows),'/',len(rows),'PASS',flush=True)