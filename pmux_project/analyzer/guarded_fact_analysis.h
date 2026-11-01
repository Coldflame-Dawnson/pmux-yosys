#ifndef PMUX_OPT_GUARDED_FACT_ANALYSIS_H
#define PMUX_OPT_GUARDED_FACT_ANALYSIS_H

#include "pmux_opt_api.h"

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {

// Internal adapter shared by the entry and branch analyzers. It deliberately
// returns the frozen v0.1 result type so both clients use one propagation
// engine and one set of resource counters.
EntryAnalysisResult analyze_guarded_values(const RTLIL::Module *module,
		const RTLIL::SigSpec &data, const std::vector<GuardFact> &guard,
		const AnalysisBudget &budget);

} // namespace PmuxOpt

YOSYS_NAMESPACE_END

#endif
