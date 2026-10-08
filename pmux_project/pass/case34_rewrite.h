#ifndef PMUX_OPT_CASE34_REWRITE_H
#define PMUX_OPT_CASE34_REWRITE_H

#include "kernel/yosys.h"

#include <map>
#include <string>
#include <vector>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {

struct Case34StructuralEdit {
	std::string module_id;
	std::string rule;
	std::string anchor_cell;
	std::string proof_reason;
	int removed_cells = 0;
	int added_cells = 0;
	int folded_bits = 0;
	std::vector<int> split_by_output_bit;
};

struct Case34RewriteResult {
	int decision_cone_candidates = 0;
	int guarded_candidates = 0;
	int table_candidates = 0;
	int proven_candidates = 0;
	int rejected_candidates = 0;
	int committed_candidates = 0;
	int removed_cells = 0;
	int added_cells = 0;
	int folded_bits = 0;
	std::map<std::string, int> reason_counts;
	std::vector<Case34StructuralEdit> edits;
};

// Discover, prove, conflict-check and atomically apply the v0.2 case3/case4
// structural rules.  All discovery and proof happens before the first RTLIL
// write.  A failed candidate is rejected with a stable reason and does not
// authorize a partial rewrite.
Case34RewriteResult run_case34_structural_rewrite(
		const std::vector<RTLIL::Module *> &modules);

} // namespace PmuxOpt

YOSYS_NAMESPACE_END

#endif
