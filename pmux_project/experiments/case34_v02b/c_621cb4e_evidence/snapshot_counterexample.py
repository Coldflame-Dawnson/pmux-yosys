#!/usr/bin/env python3
"""Fault injection required by C acceptance: alter a real committed cell before native mapping."""
import importlib.util,json
from pathlib import Path
E=Path(__file__).parent
s=importlib.util.spec_from_file_location('c_native',E/'native_acceptance.py')
# Import definitions without executing official jobs.
n={'__file__':str(E/'native_acceptance.py')};text=(E/'native_acceptance.py').read_text();exec(text[:text.index('expect=')],n)
O=n['O'];R=n['R'];d=O/'snapshot_counterexample';d.mkdir(parents=True,exist_ok=True)
commands=[f'scratchpad -set c.evidence_path {d}','scratchpad -set c.mutate_after_commit true',f'read_verilog {O}/official/test4.v','synth_intel -family cycloneiv -top test4 -run begin:map_cells','scratchpad -assert pmux_opt.accepted_constant_table false',f'write_json {d}/post_abc9.json',f'select test4',f'write_rtlil -selected {d}/gate.il']
rc,wall,log=n['ys'](commands,d,['-m',O/'build/producer_observer.so'])
actual=n['profile'](log)
result={'test':'native_changed_cell_snapshot_after_authorization','returncode':rc,'mutation_applied':'C_SNAPSHOT_MUTATION' in log,'expected_profile':'stock','actual_profile':actual,'pass':rc==0 and 'C_SNAPSHOT_MUTATION' in log and actual=='stock','log':str(d/'yosys.log')}
if rc==0:
 cfg=d/'verify.eqy';cfg.write_text(n['mod'].eqy_config(O/'official/test4.v',d/'gate.il','test4',R/'pmux_project/official_baseline/environment/dffeas_formal.v'))
 erc,ew=n['run']([n['T']/'bin/eqy','-f','-d',d/'eqy_work',cfg],d/'eqy.log')
 result['eqy_returncode']=erc;result['eqy']='PASS' if erc==0 and (d/'eqy_work/PASS').exists() else 'ERROR_OR_NOT_PROVED'
(E/'snapshot_counterexample.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2),flush=True)
raise SystemExit(0 if result['pass'] else 1)