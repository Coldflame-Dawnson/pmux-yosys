#!/usr/bin/env python3
"""C frozen Analyzer, production mutation, read-only fingerprints and table32 checks."""
import concurrent.futures,csv,hashlib,json,os,re,shlex,subprocess
from pathlib import Path
E=Path(__file__).resolve().parent;R=E.parents[3];O=R/'pmux_project/reports/c-b8368b0-default-final';D=O/'core';D.mkdir(exist_ok=False);Y=O/'runtime/bin/yosys';P=R/'pmux_project/pass/pmux_opt.so';N=O/'build/native_synth.so';A=R/'pmux_project/analyzer/analyzer_selftest.so'
ENV=dict(os.environ);ENV['PATH']=str(Y.parent)+os.pathsep+ENV.get('PATH','')
def run(name,script,plugins,extra=None):
 d=D/name;d.mkdir(exist_ok=False);ys=d/'run.ys';ys.write_text(script+'\n');env=dict(ENV);env.update(extra or {})
 cmd=[Y,'-Q',*[v for p in plugins for v in ['-m',p]],'-s',ys,'-l',d/'yosys.log']
 (d/'command.txt').write_text(shlex.join(map(str,cmd))+'\n');(d/'environment.json').write_text(json.dumps({'PMUX_ANALYZER_SLOW_TESTS':env.get('PMUX_ANALYZER_SLOW_TESTS'),'PATH':env['PATH']},indent=2)+'\n')
 with(d/'console.log').open('w')as f:cp=subprocess.run(map(str,cmd),cwd=R,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=900)
 text=(d/'yosys.log').read_text();assert cp.returncode==0,(name,cp.returncode)
 return d,text
results={}
for mode,expected in [('fast',[2116,29,60,50,81]),('slow',[2118,29,60,50,81])]:
 d,log=run('analyzer_'+mode,'help',[A],{'PMUX_ANALYZER_SLOW_TESTS':'1'} if mode=='slow' else {})
 got=[int(n) for n in re.findall(r'all checks passed \((\d+) assertions\)',log)];assert sorted(got)==sorted(expected),(mode,got)
 results['analyzer_'+mode]={'assertions':got,'pass':True};print('ANALYZER',mode,got,'PASS',flush=True)
prefix=lambda i:f'read_verilog {O}/official/test{i}.v; synth_intel -family cycloneiv -top test{i} -run begin:map_ffram; opt -fast -mux_undef -undriven -fine -full; memory_map; opt -undriven -fine; '
d,log=run('production_mutations',prefix(3)+'pmux_opt -test-decision-proof-mutations',[P,N]);assert 'mutation selftest passed=26 checks=26 failures=0' in log;results['production_mutations']={'passed':26,'checks':26};print('PRODUCTION_MUTATION_26_PASS',flush=True)
reports=[('case3_decision',3,'pmux_case3_decision_cone_proof_report','DECISION_CONE_NETLIST_UNCHANGED 1'),('case3_production',3,'pmux_case34_production_proof_report','PRODUCTION_PROOF_NETLIST_UNCHANGED 1'),('case4_production',4,'pmux_case34_production_proof_report','PRODUCTION_PROOF_NETLIST_UNCHANGED 1')]
for name,i,command,marker in reports:
 d=D/name
 script=prefix(i)+f'select test{i}; write_rtlil -selected {d}/before.il; '+command+f'; write_rtlil -selected {d}/after.il'
 d,log=run(name,script,[A]);before=hashlib.sha256((d/'before.il').read_bytes()).hexdigest();after=hashlib.sha256((d/'after.il').read_bytes()).hexdigest();assert before==after and marker in log
 results[name]={'before_sha256':before,'after_sha256':after,'fingerprint_unchanged':True}
 if i==4:
  proof={int(a):b for a,b in re.findall(r'PRODUCTION_TABLE_ROW address=(\d+) bits=([01]+)',log)}
  source=(O/'official/test4.v').read_text()
  pattern=r"5'd(\d+)\s*:\s*\{\s*dataout\s*,\s*only_5b\s*,\s*datain_5bis7\s*,\s*data_dp\s*,\s*data_dn\s*\}\s*<=\s*\{\s*6'b([01]{6})\s*,\s*1'b([01])\s*,\s*1'b([01])\s*,\s*1'b([01])\s*,\s*1'b([01])\s*\}\s*;"
  rows=re.findall(pattern,source);gold={int(a):''.join(bits) for a,*bits in rows};assert len(rows)==len(gold)==32 and set(gold)==set(range(32));assert gold==proof and gold[0]=='1001110000'
  with(E/'case4_table32.csv').open('w',newline='')as f:
   w=csv.writer(f,lineterminator='\n');w.writerow(['address','official_bits','proof_bits','pass'])
   for a in range(32):w.writerow([a,gold[a],proof[a],gold[a]==proof[a]])
  results['table32']={'rows':32,'width':10,'address0':gold[0],'all_rows_match':True}
 print('READONLY',name,'SHA256_UNCHANGED_PASS',flush=True)
(E/'core_results.json').write_text(json.dumps(results,indent=2)+'\n');print('CORE_REGRESSION_PASS',flush=True)