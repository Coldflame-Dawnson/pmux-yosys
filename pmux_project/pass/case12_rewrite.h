#ifndef PMUX_OPT_CASE12_REWRITE_H
#define PMUX_OPT_CASE12_REWRITE_H

#include "kernel/yosys.h"

#include <map>
#include <string>
#include <vector>

YOSYS_NAMESPACE_BEGIN
namespace PmuxOpt {

struct Case12RewriteResult {
	bool malformed_cell_metadata = false;
	int paired_candidates = 0;
	int priority_candidates = 0;
	int committed_candidates = 0;
	int removed_cells = 0;
	int added_cells = 0;
	std::map<std::string, int> reason_counts;
};

Case12RewriteResult run_case12_structural_rewrite(const std::vector<RTLIL::Module *> &modules);

} // namespace PmuxOpt
YOSYS_NAMESPACE_END

#endif
