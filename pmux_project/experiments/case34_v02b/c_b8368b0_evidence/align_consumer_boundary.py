from pathlib import Path
import shutil
E=Path('/home/yuewen/work/pmux-yosys-c-b8368b0/pmux_project/experiments/case34_v02b/c_b8368b0_evidence');O=E.parents[3]/'pmux_project/reports/c-b8368b0-default-final'
shutil.move(O/'lifecycle',O/'lifecycle_setup_attempt_mixed_boundary');shutil.copyfile(E/'lifecycle_fixture.cc',O/'lifecycle_setup_attempt_mixed_boundary/lifecycle_fixture.cc');shutil.copyfile(E/'lifecycle_matrix.json',O/'lifecycle_setup_attempt_mixed_boundary/lifecycle_matrix.json')
f=E/'lifecycle_fixture.cc';s=f.read_text();a=s.index('  if(mode=="capture")');b=s.index('\n }\n} producer;',a)
body=s[a:b]
helper='void apply_mutation(RTLIL::Design*d) {\n auto mode=d->scratchpad_get_string("c.mode","none");auto out=d->scratchpad_get_string("c.stage_dir","");auto before=PA::build_constant_table_snapshot(d).payload;\n'+body+'\n}\n'
s=s[:a]+'  if(d->scratchpad_get_string("c.path","")=="native")apply_mutation(d);'+s[b:]
pos=s.index('struct Producer : Pass');s=s[:pos]+helper+s[pos:]
pos=s.index('void assert_clear(');s=s[:pos]+'struct Apply : Pass {Apply():Pass("c_apply_mutation","C injects at runner consumer boundary"){}void execute(std::vector<std::string>,RTLIL::Design*d)override{apply_mutation(d);}} apply;\n'+s[pos:];f.write_text(s)
f=E/'run_lifecycle.py';s=f.read_text().replace("'pmux_opt',*common(),'scratchpad -set c.effects_guard true'","'pmux_opt',*common(),'c_apply_mutation','scratchpad -set c.effects_guard true'");f.write_text(s)