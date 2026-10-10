from pathlib import Path
import shutil
E=Path('/home/yuewen/work/pmux-yosys-c-b8368b0/pmux_project/experiments/case34_v02b/c_b8368b0_evidence');O=E.parents[3]/'pmux_project/reports/c-b8368b0-default-final'
shutil.move(O/'lifecycle',O/'lifecycle_setup_attempt_no_address_reuse');shutil.copyfile(E/'lifecycle_fixture.cc',O/'lifecycle_setup_attempt_no_address_reuse/lifecycle_fixture.cc')
f=E/'lifecycle_fixture.cc';s=f.read_text();a=s.index(' bool reused=false;int attempts=0;');b=s.index(' log("C_REBUILT_CELL',a)
s=s[:a]+''' bool reused=false;int attempts=0;
 if (allocator) {
  // Hold unrelated allocations instead of repeatedly freeing the same new block.
  // This reaches the freed target address without changing the rejection oracle.
  std::vector<std::unique_ptr<RTLIL::Cell>> probes;
  c=nullptr;
  for(;attempts<4096;attempts++) {
   auto probe=std::unique_ptr<RTLIL::Cell>(new RTLIL::Cell);
   if(reinterpret_cast<std::uintptr_t>(probe.get())==address){c=probe.release();reused=true;attempts++;break;}
   probes.push_back(std::move(probe));
  }
  log_assert(c != nullptr);c->name=name;c->type=type;m->add(c);
  c->parameters=params;c->attributes=attrs;for(auto p:ports)c->setPort(p.first,p.second);
 } else {
  c=m->addCell(name,type);c->parameters=params;c->attributes=attrs;for(auto p:ports)c->setPort(p.first,p.second);attempts=1;reused=reinterpret_cast<std::uintptr_t>(c)==address;
 }
 log_assert(!d->scratchpad_get_bool(PA::lifecycle_key,false));
'''+s[b:];f.write_text(s)