// C test-only pass observer. Production producer and synth_intel sources remain unchanged.
#include "kernel/yosys.h"
USING_YOSYS_NAMESPACE
namespace {
Pass *c_real_pmux;
struct CBind { CBind() { c_real_pmux=pass_register.at("pmux_opt"); pass_register.erase("pmux_opt"); } } c_bind;
struct CObserve : Pass {
 CObserve():Pass("pmux_opt","C observes unchanged production PMUX producer") {}
 void execute(std::vector<std::string> args, RTLIL::Design *d) override {
  std::string path=d->scratchpad_get_string("c.evidence_path","");
  if (!path.empty()) { args.push_back("-write-plan");args.push_back(path+"/plan.json");args.push_back("-write-stats");args.push_back(path+"/stats.json"); }
  c_real_pmux->execute(args,d);
  if (!path.empty()) Pass::call(d,"write_rtlil "+path+"/post_producer.il");
  if (d->scratchpad_get_bool("c.mutate_after_commit",false) && d->scratchpad_get_bool("pmux_opt.accepted_constant_table",false)) {
   for (auto m:d->selected_modules()) for(auto c:m->selected_cells()) if(c->type==ID(pmux_case34_lut4)) {
    RTLIL::Const old=c->getParam(ID(LUT)); RTLIL::Const altered=old;
    altered.bits()[0]=old.bits()[0]==RTLIL::State::S1?RTLIL::State::S0:RTLIL::State::S1;
    c->setParam(ID(LUT),altered);
    log("C_SNAPSHOT_MUTATION cell=%s old=%s altered=%s authorization=%d\n",log_id(c),log_signal(old),log_signal(altered),d->scratchpad_get_bool("pmux_opt.accepted_constant_table",false));
    if (!path.empty()) Pass::call(d,"write_rtlil "+path+"/changed_snapshot.il");
    return;
   }
  }
 }
} c_observe;
}