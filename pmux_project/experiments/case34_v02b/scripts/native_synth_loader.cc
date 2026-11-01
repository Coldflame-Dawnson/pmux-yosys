#include "kernel/register.h"

namespace {
struct ReplaceStockSynthIntel
{
	ReplaceStockSynthIntel() { Yosys::pass_register.erase("synth_intel"); }
} replace_stock_synth_intel;
}

#include "../../../../techlibs/intel/synth_intel.cc"
