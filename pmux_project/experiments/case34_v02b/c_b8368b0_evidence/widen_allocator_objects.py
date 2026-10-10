from pathlib import Path
import shutil
E=Path('/home/yuewen/work/pmux-yosys-c-b8368b0/pmux_project/experiments/case34_v02b/c_b8368b0_evidence');O=E.parents[3]/'pmux_project/reports/c-b8368b0-default-final'
shutil.move(O/'lifecycle',O/'lifecycle_setup_attempt_allocator_context');shutil.copyfile(E/'lifecycle_fixture.cc',O/'lifecycle_setup_attempt_allocator_context/lifecycle_fixture.cc')
f=E/'lifecycle_fixture.cc';s=f.read_text();a=s.index('void rebuild_cell(');b=s.index('\nvoid apply_mutation',a)
s=s[:a]+'''void rebuild_cell(RTLIL::Design*d,bool allocator) {
 std::vector<RTLIL::Cell*> targets;
 for(auto m:d->modules())for(auto c:m->cells())if(c->type==ID(pmux_case34_lut4))targets.push_back(c);
 std::sort(targets.begin(),targets.end(),[](auto a,auto b){return a->name.str()<b->name.str();});
 bool reused=false;int attempts=0,objects=0;
 for(auto old:targets) {
  auto m=old->module;auto name=old->name,type=old->type;auto params=old->parameters,attrs=old->attributes;auto ports=old->connections();auto address=reinterpret_cast<std::uintptr_t>(old);
  m->remove(old);RTLIL::Cell*c=nullptr;
  for(int trial=0;trial<(allocator?32:1);trial++){
   c=m->addCell(name,type);c->parameters=params;c->attributes=attrs;for(auto p:ports)c->setPort(p.first,p.second);attempts++;
   reused|=reinterpret_cast<std::uintptr_t>(c)==address;log_assert(!d->scratchpad_get_bool(PA::lifecycle_key,false));
   if(reused||trial==(allocator?31:0))break;m->remove(c);
  }
  objects++;if(!allocator||reused)break;
 }
 log("C_REBUILT_CELL pointer_reused=%d attempts=%d objects=%d lifecycle_valid=%d\\n",reused,attempts,objects,d->scratchpad_get_bool(PA::lifecycle_key,false));
}
'''+s[b:];f.write_text(s)
f=E/'run_lifecycle.py';s=f.read_text().replace("D=O/'lifecycle'","D=O/os.environ.get('C_LIFECYCLE_OUT','lifecycle')");s=s.replace("for path in ['native','runner']:","if os.environ.get('C_ONLY_ALLOCATOR'):modes=['allocator_reuse']\nfor path in ['native','runner']:");f.write_text(s)