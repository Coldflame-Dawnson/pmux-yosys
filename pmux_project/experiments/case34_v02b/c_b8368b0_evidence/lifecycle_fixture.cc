// C-only fault injection and side-effect observation; calls unchanged production passes.
#include "kernel/yosys.h"
#include "../../../../pmux_project/pass/pmux_profile_authorization.h"
#include <algorithm>
#include <array>
#include <memory>
#include <vector>
USING_YOSYS_NAMESPACE
namespace PA=PmuxProfileAuthorization;
namespace {
Pass *real_pmux,*real_techmap,*real_abc9;
struct Bind { Bind() { real_pmux=pass_register.at("pmux_opt");real_techmap=pass_register.at("techmap");real_abc9=pass_register.at("abc9");for(auto n:{"pmux_opt","techmap","abc9"})pass_register.erase(n); } } bind;
std::unique_ptr<RTLIL::Design> saved_design;
std::string saved_name;
std::array<std::string,4> saved_fields;
std::vector<std::unique_ptr<RTLIL::Design>> foreign_designs;
const std::array<const char*,4> keys={PA::enabled_key,PA::snapshot_key,PA::generation_key,PA::lifecycle_key};
RTLIL::Cell *target(RTLIL::Design*d) {
 std::vector<RTLIL::Cell*> v;for(auto m:d->modules())for(auto c:m->cells())if(c->type==ID(pmux_case34_lut4))v.push_back(c);
 std::sort(v.begin(),v.end(),[](auto a,auto b){return a->name.str()<b->name.str();});return v.empty()?nullptr:v[0];
}
void observe(RTLIL::Design*d,const std::string&mode,const std::string&before) {
 auto current=PA::build_constant_table_snapshot(d);
 log("C_BEFORE_CONSUME mode=%s payload_equal=%d authorized=%d lifecycle_valid=%d lifecycle_monitor_match=%d generation=%s target_count=%d\n",mode.c_str(),current.payload==before,d->scratchpad_get_bool(PA::enabled_key,false),d->scratchpad_get_bool(PA::lifecycle_key,false),PA::constant_table_lifecycle_matches(d),d->scratchpad_get_string(PA::generation_key,"").c_str(),current.cell_count);
 auto out=d->scratchpad_get_string("c.stage_dir","");if(!out.empty())Pass::call(d,"write_rtlil "+out+"/pre_consume_"+mode+".il");
}
void rebuild_cell(RTLIL::Design*d,bool allocator) {
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
 log("C_REBUILT_CELL pointer_reused=%d attempts=%d objects=%d lifecycle_valid=%d\n",reused,attempts,objects,d->scratchpad_get_bool(PA::lifecycle_key,false));
}

void apply_mutation(RTLIL::Design*d) {
 auto mode=d->scratchpad_get_string("c.mode","none");auto out=d->scratchpad_get_string("c.stage_dir","");auto before=PA::build_constant_table_snapshot(d).payload;
  if(mode=="capture") {saved_design.reset(new RTLIL::Design);saved_name=d->top_module()->name.str();saved_design->add(d->top_module()->clone());for(int i=0;i<4;i++)saved_fields[i]=d->scratchpad_get_string(keys[i],"");}
  else if(mode=="restore_old_after_producer"){for(int i=0;i<4;i++)d->scratchpad_set_string(keys[i],saved_fields[i]);}
  else if(mode=="forge"||mode=="forge_suffix"||mode=="forge_numeric"){
   auto original=d->scratchpad_get_string(PA::generation_key,"");std::string token=mode=="forge_suffix"?original+"-different":mode=="forge_numeric"?"1:999999999":"C-forged-generation";log_assert(!token.empty()&&token!=original);d->scratchpad_set_string(PA::generation_key,token);
  }else if(mode=="empty_generation")d->scratchpad_set_string(PA::generation_key,"");
  else if(mode=="lifecycle_false")d->scratchpad_set_bool(PA::lifecycle_key,false);
  else if(mode=="drop_snapshot")d->scratchpad_set_string(PA::snapshot_key,"");
  else if(mode=="auth_false")d->scratchpad_set_bool(PA::enabled_key,false);
  else if(mode=="unknown_auth")d->scratchpad_set_string(PA::enabled_key,"unknown-profile");
  else if(mode=="unregister") {auto mon=PA::registered_lifecycle_monitor(d);log_assert(mon);d->monitors.erase(const_cast<PA::LifecycleMonitor*>(mon));}
  else if(mode=="foreign_generation") {
   auto other=std::unique_ptr<RTLIL::Design>(new RTLIL::Design);other->add(d->top_module()->clone());log_assert(PA::publish_constant_table_authorization(other.get(),true));
   auto foreign=other->scratchpad_get_string(PA::generation_key,"");log_assert(foreign!=d->scratchpad_get_string(PA::generation_key,""));d->scratchpad_set_string(PA::generation_key,foreign);
   log("C_FOREIGN_GENERATION foreign_design_distinct=%d foreign_monitor_match=%d\n",other.get()!=d,PA::constant_table_lifecycle_matches(other.get()));foreign_designs.push_back(std::move(other));
  }else if(mode=="old_generation"||mode=="second_generation") {
   auto old=d->scratchpad_get_string(PA::generation_key,"");auto oldmon=PA::registered_lifecycle_monitor(d);log_assert(oldmon);
   log_assert(PA::publish_constant_table_authorization(d,true));auto now=d->scratchpad_get_string(PA::generation_key,"");log_assert(now!=old&& !now.empty());
   log("C_TWO_PUBLICATIONS distinct=1 old_registration_removed=%d new_monitor_match=%d\n",!d->monitors.count(const_cast<PA::LifecycleMonitor*>(oldmon)),PA::constant_table_lifecycle_matches(d));
   if(mode=="old_generation")d->scratchpad_set_string(PA::generation_key,old);
  }else if(mode=="same_name_rebuild"||mode=="allocator_reuse")rebuild_cell(d,mode=="allocator_reuse");
  else if(mode=="module_rebuild") {auto m=target(d)->module;auto clone=m->clone();d->remove(m);d->add(clone);}
  else if(mode=="port_restore") {auto c=target(d);auto original=c->getPort(ID::A);auto altered=original;altered[0]=RTLIL::State::S0;c->setPort(ID::A,altered);c->setPort(ID::A,original);}
  else if(mode=="lut_bit"||mode=="lut_width") {auto c=target(d);auto bits=c->getParam(ID(LUT)).to_bits();if(mode=="lut_bit")bits[0]=bits[0]==RTLIL::State::S1?RTLIL::State::S0:RTLIL::State::S1;else bits.push_back(RTLIL::State::S0);c->setParam(ID(LUT),RTLIL::Const(bits));}
  else if(mode=="attribute")target(d)->set_bool_attribute(ID(c_changed));
  else if(mode=="type") {auto c=target(d);c->type=ID($lut);c->setParam(ID::WIDTH,4);}
  else if(mode=="port_order") {auto c=target(d);auto p=c->getPort(ID::A);std::swap(p[0],p[1]);c->setPort(ID::A,p);}
  else if(mode=="remove_one") {auto c=target(d);c->module->remove(c);}
  else if(mode=="remove_all") {for(auto m:d->modules()){std::vector<RTLIL::Cell*>v;for(auto c:m->cells())if(c->type==ID(pmux_case34_lut4))v.push_back(c);for(auto c:v)m->remove(c);}}
  else if(mode=="add_extra") {auto m=target(d)->module;auto w=m->addWire(NEW_ID);auto c=m->addCell(NEW_ID,ID(pmux_case34_lut4));c->setParam(ID(LUT),RTLIL::Const(0x6996,16));c->setPort(ID::A,RTLIL::SigSpec(RTLIL::State::S0,4));c->setPort(ID::Y,w);c->set_bool_attribute(ID::keep);}
  else if(mode!="none"&&mode!="stub_unknown"&&mode!="disable")log_cmd_error("C unsupported mode %s\n",mode.c_str());
  observe(d,mode,before);
  if(d->scratchpad_get_string("c.path","")=="native")d->scratchpad_set_bool("c.effects_guard",true);
}
struct Producer : Pass {
 Producer():Pass("pmux_opt","C observes genuine authorization publication"){}
 void execute(std::vector<std::string>args,RTLIL::Design*d)override{
  auto mode=d->scratchpad_get_string("c.mode","none");auto out=d->scratchpad_get_string("c.stage_dir","");
  if(mode=="stub_unknown")args.push_back("-stub-unknown");if(mode=="disable")args.push_back("-no-v02-case34");
  if(!out.empty()){args.push_back("-write-plan");args.push_back(out+"/plan.json");args.push_back("-write-stats");args.push_back(out+"/stats.json");}
  real_pmux->execute(args,d);auto before=PA::build_constant_table_snapshot(d).payload;
  if(!out.empty())Pass::call(d,"write_rtlil "+out+"/post_producer_"+mode+".il");
  if(d->scratchpad_get_string("c.path","")=="native")apply_mutation(d);
 }
} producer;
struct Apply : Pass {Apply():Pass("c_apply_mutation","C injects at runner consumer boundary"){}void execute(std::vector<std::string>,RTLIL::Design*d)override{apply_mutation(d);}} apply;
void assert_clear(RTLIL::Design*d,const char*effect){
 if(!d->scratchpad_get_bool("c.effects_guard",false))return;
 log_assert(!d->scratchpad_get_bool(PA::enabled_key,true));log_assert(d->scratchpad_get_string(PA::snapshot_key,"missing").empty());log_assert(d->scratchpad_get_string(PA::generation_key,"missing").empty());log_assert(!d->scratchpad_get_bool(PA::lifecycle_key,true));
 log("C_CLEAR_BEFORE_EFFECT effect=%s authorization=0 snapshot_empty=1 generation_empty=1 lifecycle_valid=0\n",effect);
}
struct Techmap : Pass {Techmap():Pass("techmap","C observes clearing before unchanged techmap"){}void execute(std::vector<std::string>a,RTLIL::Design*d)override{assert_clear(d,"techmap");real_techmap->execute(a,d);}} techmap;
struct Abc9 : Pass {Abc9():Pass("abc9","C observes clearing before unchanged ABC9"){}void execute(std::vector<std::string>a,RTLIL::Design*d)override{assert_clear(d,"abc9");real_abc9->execute(a,d);}} abc9;
struct Restore : Pass {
 Restore():Pass("c_restore_targets","C reintroduces exact pre-consumption target contents"){}
 void execute(std::vector<std::string>a,RTLIL::Design*d)override{
  log_assert(saved_design != nullptr);auto name=RTLIL::IdString(saved_name);auto old=d->module(name);if(old)d->remove(old);d->add(saved_design->module(name)->clone());
  bool replay=a.size()>1&&a[1]=="old";if(replay)for(int i=0;i<4;i++)d->scratchpad_set_string(keys[i],saved_fields[i]);
  auto payload=PA::build_constant_table_snapshot(d).payload;log("C_RESTORED_FULL_TARGETS payload_equal=%d old_auth_replayed=%d lifecycle_monitor_match=%d\n",payload==saved_fields[1],replay,PA::constant_table_lifecycle_matches(d));log_assert(payload==saved_fields[1]);
 }
} restore;
struct Cleanup : Pass {Cleanup():Pass("c_cleanup_foreign","C releases fixture foreign designs"){}void execute(std::vector<std::string>,RTLIL::Design*)override{for(auto&d:foreign_designs)PA::clear_constant_table_authorization(d.get());foreign_designs.clear();}} cleanup;
}