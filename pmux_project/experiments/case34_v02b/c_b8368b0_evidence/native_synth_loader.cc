#include "kernel/register.h"
namespace { struct CReplaceStock { CReplaceStock() { Yosys::pass_register.erase("synth_intel"); } } c_replace_stock; }
#include "../../../../techlibs/intel/synth_intel.cc"
