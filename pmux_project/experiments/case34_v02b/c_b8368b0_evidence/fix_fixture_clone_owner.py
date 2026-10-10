from pathlib import Path
import shutil
E=Path('/home/yuewen/work/pmux-yosys-c-b8368b0/pmux_project/experiments/case34_v02b/c_b8368b0_evidence');O=E.parents[3]/'pmux_project/reports/c-b8368b0-default-final'
shutil.move(O/'lifecycle',O/'lifecycle_setup_attempt_unowned_clone');shutil.copyfile(E/'lifecycle_fixture.cc',O/'lifecycle_setup_attempt_unowned_clone/lifecycle_fixture.cc')
f=E/'lifecycle_fixture.cc';s=f.read_text().replace('std::unique_ptr<RTLIL::Module> saved_top;','std::unique_ptr<RTLIL::Design> saved_design;\nstd::string saved_name;')
s=s.replace('saved_top.reset(d->top_module()->clone());','saved_design.reset(new RTLIL::Design);saved_name=d->top_module()->name.str();saved_design->add(d->top_module()->clone());')
s=s.replace('log_assert(saved_top != nullptr);auto name=saved_top->name;auto old=d->module(name);if(old)d->remove(old);d->add(saved_top->clone());','log_assert(saved_design != nullptr);auto name=RTLIL::IdString(saved_name);auto old=d->module(name);if(old)d->remove(old);d->add(saved_design->module(name)->clone());')
s=s.replace('out+"/pre_consume.il"','out+"/pre_consume_"+mode+".il"')
s=s.replace('if(!out.empty())Pass::call(d,"write_rtlil "+out+"/post_producer.il");','if(!out.empty())Pass::call(d,"write_rtlil "+out+"/post_producer_"+mode+".il");')
f.write_text(s)